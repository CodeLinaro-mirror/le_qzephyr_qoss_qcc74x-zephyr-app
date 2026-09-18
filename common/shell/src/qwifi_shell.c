/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/shell/shell.h>
#include <zephyr/net/wifi_mgmt.h>
#include <errno.h>
#include <string.h>

#include <wifi_mgmr_ext.h>
#include "wifi_mgmr.h"
#include "wifi_mgmr_pmk.h"
#include "wireless_api.h"
/*
 * Provides union fhost_rc_rate_ctrl_info (the packed word wifi_mgmr_rate_config()
 * takes), the FORMATMOD_* codes and, through export/mac/mac.h, the FIRST_*
 * boundaries of the MAC rate-control table.
 */
#include "fhost.h"

/*
 * wifi_mgmr_sta_ap_retry_limit_get() returns a uint32_t whose upper bits are
 * junk once a limit has been programmed: measured on hardware, set(12) reads
 * back 7290892 and set(5) reads back 7290885, i.e. 0x6F4400 | value. Only the
 * low byte is meaningful.
 */
#define QCOM_WIFI_RETRY_LIMIT_MASK 0xFFU

/* Tx power is handled by the fhost stack in units of 0.5 dBm. */
#define QCOM_WIFI_TX_POWER_STEPS_PER_DBM 2

/*
 * wifi_mgmr_sta_ap_tx_power_set() documents no range, so bound the input here:
 * 20 dBm is the board's own ceiling, taken from pwr_table_11b in the factory
 * params dts (OFDM/HT/HE only reach 18). The per-rate TPC table clamps again
 * below this, so anything higher is meaningless and only risks overflowing the
 * dBm -> half-dBm conversion on a typo.
 */
#define QCOM_WIFI_TX_POWER_MIN_DBM 0
#define QCOM_WIFI_TX_POWER_MAX_DBM 20

#define QCOM_WIFI_TX_POWER_MAX_STEPS (QCOM_WIFI_TX_POWER_MAX_DBM * QCOM_WIFI_TX_POWER_STEPS_PER_DBM)

/*
 * Sentinel for "drop the explicit power". tx_power_get() reports that state as
 * -1; whether the setter honours it is undocumented, hence the probe wording in
 * the command help.
 */
#define QCOM_WIFI_TX_POWER_DEFAULT_STEPS (-1)

/* wifi_mgmr_set_mode()/wifi_mgmr_get_mode() interface selector. */
#define QCOM_WIFI_MODE_IFACE_STA 0U
#define QCOM_WIFI_MODE_IFACE_AP  1U

struct qcom_wifi_phy_mode {
	const char *name;
	uint32_t mode;
};

/*
 * QCC743 is a 2.4 GHz only part, and wifi_mgmr_set_mode() only accepts a
 * contiguous subset from 11b up. WIFI_MODE_BGAX is therefore omitted: it sets
 * 11ax without 11n and the stack rejects it with -1.
 */
static const struct qcom_wifi_phy_mode qcom_wifi_phy_modes[] = {
	{ "b", WIFI_MODE_B },
	{ "bg", WIFI_MODE_BG },
	{ "bgn", WIFI_MODE_BGN },
	{ "bgnax", WIFI_MODE_BGNAX },
};

/* Sentinel that wifi_mgmr_rate_config() reads as "let rate control pick". */
#define QCOM_WIFI_RATE_AUTO 0xFFFFU

/* Highest HE GI/LTF selector accepted by gi_and_pre_type_tx. */
#define QCOM_WIFI_RATE_GI_MAX 2

/*
 * QCC74x is 2.4 GHz only, so only PHY_CHNL_BW_20MHZ (0) and BW_40MHZ (1) are
 * reachable; table indices that decode to 80/160 MHz are rejected.
 */
#define QCOM_WIFI_RATE_BW_MAX 1

/*
 * Legacy OFDM occupies mcs_idx 4..11 of the NON_HT format. wifi_mgmr_rate_config()
 * returns 0 for those but the link keeps its previous rate, measured on hardware,
 * so the command warns instead of reporting a clean success.
 */
#define QCOM_WIFI_RATE_FIRST_OFDM_MCS_IDX 4

static void qcom_wifi_print_mac(const struct shell *sh, const char *label, const uint8_t *mac)
{
	shell_print(sh, "%-16s %02x:%02x:%02x:%02x:%02x:%02x", label, mac[0], mac[1], mac[2],
		    mac[3], mac[4], mac[5]);
}

/*
 * wifi_mgmr_sta_ap_tx_power_get() returns a negative value when no explicit tx
 * power has been programmed, so the half-dBm formatting must not be applied to
 * it (it would render -1 as "0.5 dBm").
 */
static void qcom_wifi_print_tx_power(const struct shell *sh, const char *label, int steps)
{
	if (steps < 0) {
		shell_print(sh, "%-16s not set (raw %d)", label, steps);
		return;
	}

	shell_print(sh, "%-16s %d.%u dBm (%d x 0.5dBm)", label,
		    steps / QCOM_WIFI_TX_POWER_STEPS_PER_DBM,
		    (steps % QCOM_WIFI_TX_POWER_STEPS_PER_DBM) != 0 ? 5U : 0U, steps);
}

/* wifi_mgmr_get_mode() returns a negative error code, which must not be fed to
 * wifi_mgmr_mode_to_str() as a bitmask.
 */
static void qcom_wifi_print_phy_mode(const struct shell *sh, const char *label, int mode)
{
	if (mode < 0) {
		shell_print(sh, "%-16s unavailable (%d)", label, mode);
		return;
	}

	shell_print(sh, "%-16s %s (0x%x)", label, wifi_mgmr_mode_to_str((uint32_t)mode), mode);
}

static int qcom_wifi_parse_mode(const struct shell *sh, const char *arg, uint32_t *mode)
{
	unsigned long raw;
	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(qcom_wifi_phy_modes); i++) {
		if (strcmp(arg, qcom_wifi_phy_modes[i].name) == 0) {
			*mode = qcom_wifi_phy_modes[i].mode;
			return 0;
		}
	}

	/* Fall back to a raw bitmask so uncommon combinations stay reachable. */
	raw = shell_strtoul(arg, 0, &err);
	if (err != 0 || raw == 0UL || raw > 0x1FFUL) {
		shell_error(sh, "Unknown phy mode '%s'", arg);
		return -EINVAL;
	}

	*mode = (uint32_t)raw;

	return 0;
}

static int qcom_wifi_parse_iface(const struct shell *sh, const char *arg, uint8_t *iface)
{
	if (strcmp(arg, "sta") == 0) {
		*iface = QCOM_WIFI_MODE_IFACE_STA;
		return 0;
	}

	if (strcmp(arg, "ap") == 0) {
		*iface = QCOM_WIFI_MODE_IFACE_AP;
		return 0;
	}

	shell_error(sh, "Expected 'sta' or 'ap', got '%s'", arg);

	return -EINVAL;
}

/* ------------------------------------------------------------------------- */
/* Device info                                                               */
/* ------------------------------------------------------------------------- */

static int cmd_qcom_wifi_info(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t mac[6] = { 0 };
	char country[3] = { 0 };
	uint8_t c24 = 0U;
	uint8_t c5 = 0U;
	wifi_mgmr_connect_ind_stat_info_t stat = { 0 };
	int rssi = 0;
	int sta_state;
	int twt;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "wireless         %s", WIRELESS_VERSION);

	if (wifi_mgmr_sta_mac_get(mac) == 0) {
		qcom_wifi_print_mac(sh, "sta mac", mac);
	}

	if (wifi_mgmr_ap_mac_get(mac) == 0) {
		qcom_wifi_print_mac(sh, "ap mac", mac);
	}

	sta_state = wifi_mgmr_sta_state_get();
	shell_print(sh, "%-16s %s (raw %d)", "sta state", sta_state == 1 ? "connected" : "idle",
		    wifi_mgmr_sta_state_get_ext());
	shell_print(sh, "%-16s %s (raw %d)", "ap state",
		    wifi_mgmr_ap_state_get() == 1 ? "started" : "stopped",
		    wifi_mgmr_ap_state_get_ext());

	qcom_wifi_print_phy_mode(sh, "sta phy mode", wifi_mgmr_get_mode(QCOM_WIFI_MODE_IFACE_STA));
	qcom_wifi_print_phy_mode(sh, "ap phy mode", wifi_mgmr_get_mode(QCOM_WIFI_MODE_IFACE_AP));

	if (wifi_mgmr_get_country_code(country) == 0) {
		wifi_mgmr_get_channel_nums(country, &c24, &c5);
		shell_print(sh, "%-16s %s (2.4G ch %u, 5G ch %u)", "country", country, c24, c5);
	}

	qcom_wifi_print_tx_power(sh, "tx power", wifi_mgmr_sta_ap_tx_power_get());
	shell_print(sh, "%-16s %u", "retry limit",
		    wifi_mgmr_sta_ap_retry_limit_get() & QCOM_WIFI_RETRY_LIMIT_MASK);
	shell_print(sh, "%-16s %u", "listen itv", wifi_mgmr_sta_get_listen_itv());

	twt = wifi_mgmr_twt_support();
	shell_print(sh, "%-16s %s", "twt support",
		    twt == 1 ? "yes" : (twt == 0 ? "no" : "unknown"));

	if (sta_state == 1) {
		if (wifi_mgmr_sta_rssi_get(&rssi) == 0) {
			shell_print(sh, "%-16s %d dBm", "rssi", rssi);
		}

		/*
		 * wifi_mgmr_sta_connect_ind_stat_get() writes its own multi-line
		 * dump straight to the console, which interleaves with anything
		 * printed around it, so it is called last and its fields are not
		 * re-formatted here. Use "wifi status" for a parseable version.
		 */
		shell_print(sh, "-- stack connection dump --");
		(void)wifi_mgmr_sta_connect_ind_stat_get(&stat);
	} else {
		shell_print(sh, "%-16s 0x%04x", "last status", wifi_mgmr_sta_info_status_code_get());
	}

	return 0;
}

static int cmd_qcom_wifi_remaining_tx(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t tx0 = 0U;
	uint8_t tx1 = 0U;
	uint8_t tx2 = 0U;
	uint8_t tx3 = 0U;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = wifi_mgmr_get_remaining_tx(&tx0, &tx1, &tx2, &tx3);
	if (ret != 0) {
		shell_error(sh, "Unable to read tx queues (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "pending tx: bk=%u be=%u vi=%u vo=%u", tx0, tx1, tx2, tx3);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Tx power                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * Parse "<dBm>" or "<dBm>.<0|5>" into the half-dBm steps
 * wifi_mgmr_sta_ap_tx_power_set() takes. strtod() is avoided to keep float
 * formatting out of the shell binary.
 */
static int qcom_wifi_parse_tx_power(const struct shell *sh, const char *arg, int *steps)
{
	const char *dot = strchr(arg, '.');
	char whole[8];
	size_t len = dot != NULL ? (size_t)(dot - arg) : strlen(arg);
	int half = 0;
	long power_dbm;
	int err = 0;

	if (strcmp(arg, "default") == 0) {
		*steps = QCOM_WIFI_TX_POWER_DEFAULT_STEPS;
		return 0;
	}

	/* '-' is rejected here because "-0.5" would otherwise parse to +0.5. */
	if (len == 0U || len >= sizeof(whole) || arg[0] == '-') {
		shell_error(sh, "Unable to parse <power_dBm> '%s'", arg);
		return -EINVAL;
	}

	memcpy(whole, arg, len);
	whole[len] = '\0';

	if (dot != NULL) {
		if (strcmp(dot, ".5") == 0) {
			half = 1;
		} else if (strcmp(dot, ".0") != 0) {
			shell_error(sh, "Only .0 and .5 fractions are supported, got '%s'", dot);
			return -EINVAL;
		}
	}

	power_dbm = shell_strtol(whole, 10, &err);
	if (err) {
		shell_error(sh, "Unable to parse <power_dBm> (err %d)", err);
		return err;
	}

	/* Bound the dBm before scaling so that the multiply cannot overflow. */
	if (power_dbm < QCOM_WIFI_TX_POWER_MIN_DBM || power_dbm > QCOM_WIFI_TX_POWER_MAX_DBM) {
		shell_error(sh, "Expected <power_dBm> in %d..%d", QCOM_WIFI_TX_POWER_MIN_DBM,
			    QCOM_WIFI_TX_POWER_MAX_DBM);
		return -EINVAL;
	}

	*steps = (int)power_dbm * QCOM_WIFI_TX_POWER_STEPS_PER_DBM + half;

	/* Catches the one case the dBm check lets through: MAX dBm plus a .5. */
	if (*steps > QCOM_WIFI_TX_POWER_MAX_STEPS) {
		shell_error(sh, "Expected <power_dBm> in %d..%d", QCOM_WIFI_TX_POWER_MIN_DBM,
			    QCOM_WIFI_TX_POWER_MAX_DBM);
		return -EINVAL;
	}

	return 0;
}

/*
 * No power policy argument: the setter takes the power alone and nothing in
 * reach clamps it, so the 0..20 dBm bound above is the only ceiling.
 */
static int cmd_qcom_wifi_set_tx_power(const struct shell *sh, size_t argc, char **argv)
{
	int steps = 0;
	int ret;

	ARG_UNUSED(argc);

	ret = qcom_wifi_parse_tx_power(sh, argv[1], &steps);
	if (ret != 0) {
		return ret;
	}

	ret = wifi_mgmr_sta_ap_tx_power_set(steps);
	if (ret != 0) {
		shell_error(sh, "Unable to set tx power (%d)", ret);
		return -EIO;
	}

	if (steps == QCOM_WIFI_TX_POWER_DEFAULT_STEPS) {
		shell_print(sh, "Requested restore to default (raw %d); confirm with get_tx_power",
			    steps);
	} else {
		qcom_wifi_print_tx_power(sh, "tx power set to", steps);
	}

	return 0;
}

static int cmd_qcom_wifi_get_tx_power(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qcom_wifi_print_tx_power(sh, "tx power", wifi_mgmr_sta_ap_tx_power_get());

	return 0;
}

static void qcom_wifi_print_pwr_row(const struct shell *sh, const char *label, const int8_t *vals,
				    size_t count)
{
	char line[96];
	size_t pos = 0;

	for (size_t i = 0; i < count && pos < sizeof(line) - 1U; i++) {
		int written = snprintk(&line[pos], sizeof(line) - pos, "%s%d", i == 0U ? "" : " ",
				       vals[i]);

		if (written < 0 || (size_t)written >= sizeof(line) - pos) {
			break;
		}

		pos += (size_t)written;
	}

	shell_print(sh, "  %-14s %s", label, line);
}

static int cmd_qcom_wifi_tpc_show(const struct shell *sh, size_t argc, char **argv)
{
	rf_pwr_table_t table;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memset(&table, 0, sizeof(table));

	ret = wifi_mgmr_tpc_pwr_get(&table);
	if (ret != 0) {
		shell_error(sh, "Unable to read power table (%d)", ret);
		return -EIO;
	}

	/*
	 * Unlike wifi_mgmr_sta_ap_tx_power_get(), this table is in whole dBm:
	 * the values read back match the board's factory params dts
	 * (pwr_table_11b = <20 20 20 20>) byte for byte, with no conversion.
	 */
	shell_print(sh, "power table (unit dBm):");
	qcom_wifi_print_pwr_row(sh, "11b", table.pwr_11b, ARRAY_SIZE(table.pwr_11b));
	qcom_wifi_print_pwr_row(sh, "11g", table.pwr_11g, ARRAY_SIZE(table.pwr_11g));
	qcom_wifi_print_pwr_row(sh, "11n_ht20", table.pwr_11n_ht20, ARRAY_SIZE(table.pwr_11n_ht20));
	qcom_wifi_print_pwr_row(sh, "11n_ht40", table.pwr_11n_ht40, ARRAY_SIZE(table.pwr_11n_ht40));
	qcom_wifi_print_pwr_row(sh, "11ax_he20", table.pwr_11ax_he20,
				ARRAY_SIZE(table.pwr_11ax_he20));
	qcom_wifi_print_pwr_row(sh, "11ax_he40", table.pwr_11ax_he40,
				ARRAY_SIZE(table.pwr_11ax_he40));

	return 0;
}

/* ------------------------------------------------------------------------- */
/* PHY mode / rate / retry                                                   */
/* ------------------------------------------------------------------------- */

static int cmd_qcom_wifi_set_phy_mode(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t iface;
	uint32_t mode;
	int ret;

	ARG_UNUSED(argc);

	if (qcom_wifi_parse_iface(sh, argv[1], &iface) != 0) {
		return -EINVAL;
	}

	if (qcom_wifi_parse_mode(sh, argv[2], &mode) != 0) {
		return -EINVAL;
	}

	ret = wifi_mgmr_set_mode(iface, (int)mode);
	if (ret != 0) {
		shell_error(sh, "Unable to set phy mode (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "%s phy mode set to %s (0x%x)", argv[1], wifi_mgmr_mode_to_str(mode), mode);

	return 0;
}

static int cmd_qcom_wifi_get_phy_mode(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t iface = QCOM_WIFI_MODE_IFACE_STA;

	if (argc == 2 && qcom_wifi_parse_iface(sh, argv[1], &iface) != 0) {
		return -EINVAL;
	}

	qcom_wifi_print_phy_mode(sh,
				 iface == QCOM_WIFI_MODE_IFACE_AP ? "ap phy mode" : "sta phy mode",
				 wifi_mgmr_get_mode(iface));

	return 0;
}

/*
 * Turn a rate-control table index into the packed word the firmware wants. The
 * index is the position in the MAC's linear rate table, which enumerates every
 * format/MCS/bandwidth/GI/NSS/DCM combination in that order, so the arithmetic
 * below is the same unpacking the vendor `soc_wifi rc -f` command does (see
 * qcc74x-zephyr/drivers/wifi/qc7xx/src/qc7xx_wifi_shell.c) and the indices from
 * the 20M and 40M sheets of the vendor rate-control table spreadsheet apply
 * unchanged. The indices worth using on this part:
 *
 *   Mode            BW      MCS / rate            Indices
 *   --------------- ------- --------------------- ---------------------------
 *   DSSS/CCK        20 MHz  1, 2, 5.5, 11 Mbps    1, 3, 5, 7
 *   OFDM            20 MHz  6, 9, 12, 18,         8 - 15
 *                           24, 36, 48, 54 Mbps
 *   HT_MF, LGI      20 MHz  MCS0 - MCS7           16, 20, 24, 28, 32, 36, 40,
 *                                                 44
 *   HT_MF, LGI      40 MHz  MCS0 - MCS7           18, 22, 26, 30, 34, 38, 42,
 *                                                 46
 *   VHT, LGI        20 MHz  MCS0 - MCS8           144, 152, 160, 168, 176,
 *                                                 184, 192, 200, 208
 *   VHT, LGI        40 MHz  MCS0 - MCS8           146, 154, 162, 170, 178,
 *                                                 186, 194, 202, 210
 *   HE_SU, 1.6us GI 20 MHz  MCS0 - MCS11          785, 797, 809, 821, 833,
 *                                                 845, 857, 869, 881, 893,
 *                                                 905, 917
 *   HE_SU, 1.6us GI 40 MHz  MCS0 - MCS11          788, 800, 812, 824, 836,
 *                                                 848, 860, 872, 884, 896,
 *                                                 908, 920
 *   HE_SU DCM       20 MHz  MCS0, 1, 3, 4         1361, 1373, 1397, 1409
 *   HE_ER, 1.6us GI 20 MHz  MCS0 - MCS2           3665, 3668, 3671
 *   HE_ER DCM       20 MHz  MCS0 - MCS1           3683, 3686
 *
 * The spreadsheet's 80M sheet does not apply: QCC74x is 2.4 GHz only, so any
 * index that decodes to more than 40 MHz is rejected below.
 *
 * Anything at or past FIRST_HE_MU but below FIRST_HE_ER is multi-user HE, which
 * a single link cannot be pinned to, hence -ENOTSUP.
 */
static int qcom_wifi_rate_idx_to_cfg(int idx, uint16_t *cfg)
{
	/*
	 * mcs_idx is one flat 7-bit field in fhost_rc_rate_ctrl_info; for HT and
	 * VHT/HE it is really an MCS plus a stream count, so overlay it.
	 */
	union qcom_wifi_rate_mcs_index {
		struct {
			uint32_t mcs : 3;
			uint32_t nss : 2;
		} ht;
		struct {
			uint32_t mcs : 4;
			uint32_t nss : 3;
		} vht;
	};
	/*
	 * The FIRST_* macros in mac.h are unparenthesised sums, so they must be
	 * bound to a variable before being subtracted from anything.
	 */
	const uint16_t first_ofdm = FIRST_OFDM;
	const uint16_t first_ht = FIRST_HT;
	const uint16_t first_vht = FIRST_VHT;
	const uint16_t first_he_su = FIRST_HE_SU;
	const uint16_t first_he_mu = FIRST_HE_MU;
	const uint16_t first_he_er = FIRST_HE_ER;
	/* Table entries per MCS: bandwidths * GIs, per format. */
	const uint8_t ht_rates_per_mcs = 2 * 2;
	const uint8_t vht_rates_per_mcs = 4 * 2;
	const uint8_t he_su_rates_per_mcs = 4 * 3;
	const uint8_t he_er_rates_per_mcs = 3;
	const uint8_t max_vht_mcs = 10;
	const uint8_t max_he_mcs = 12;
	union fhost_rc_rate_ctrl_info rc = { .value = 0 };
	union qcom_wifi_rate_mcs_index *rate = (union qcom_wifi_rate_mcs_index *)&rc;

	if (idx < 0 || idx >= TOT_RATES) {
		*cfg = QCOM_WIFI_RATE_AUTO;
		return 0;
	}

	if (idx < first_ofdm) {
		/* CCK: the odd index of each pair is the short-preamble variant. */
		rc.format_mod_tx = FORMATMOD_NON_HT;
		rc.gi_and_pre_type_tx = (idx & 1) << 1;
		rc.mcs_idx = idx / 2;
	} else if (idx < first_ht) {
		rc.format_mod_tx = FORMATMOD_NON_HT;
		rc.mcs_idx = idx - N_CCK + QCOM_WIFI_RATE_FIRST_OFDM_MCS_IDX;
	} else if (idx < first_vht) {
		int rel = idx - first_ht;

		rc.format_mod_tx = FORMATMOD_HT_MF;
		rate->ht.nss = rel / (8 * ht_rates_per_mcs);
		rate->ht.mcs = (rel % (8 * ht_rates_per_mcs)) / ht_rates_per_mcs;
		rc.bw_tx = ((rel % (8 * ht_rates_per_mcs)) % ht_rates_per_mcs) / 2;
		rc.gi_and_pre_type_tx = rel & 1;
	} else if (idx < first_he_su) {
		int rel = idx - first_vht;

		rc.format_mod_tx = FORMATMOD_VHT;
		rate->vht.nss = rel / (max_vht_mcs * vht_rates_per_mcs);
		rate->vht.mcs = (rel % (max_vht_mcs * vht_rates_per_mcs)) / vht_rates_per_mcs;
		rc.bw_tx = ((rel % (max_vht_mcs * vht_rates_per_mcs)) % vht_rates_per_mcs) / 2;
		rc.gi_and_pre_type_tx = rel & 1;
	} else if (idx < first_he_mu) {
		int rel = idx - first_he_su;

		rc.format_mod_tx = FORMATMOD_HE_SU;
		rc.dcm_tx = rel / (4 * max_he_mcs * he_su_rates_per_mcs);
		rate->vht.nss = (rel % (4 * max_he_mcs * he_su_rates_per_mcs)) /
				(max_he_mcs * he_su_rates_per_mcs);
		rate->vht.mcs = (rel % (max_he_mcs * he_su_rates_per_mcs)) / he_su_rates_per_mcs;
		rc.bw_tx = ((rel % (max_he_mcs * he_su_rates_per_mcs)) % he_su_rates_per_mcs) / 3;
		rc.gi_and_pre_type_tx = rel % 3;
	} else if (idx < first_he_er) {
		return -ENOTSUP;
	} else {
		int rel = idx - first_he_er;

		rc.format_mod_tx = FORMATMOD_HE_ER;
		rc.dcm_tx = rel / (2 * 3 * he_er_rates_per_mcs);
		rc.bw_tx = (rel % (2 * 3 * he_er_rates_per_mcs)) / (3 * he_er_rates_per_mcs);
		rc.gi_and_pre_type_tx = rel % 3;
		rate->vht.mcs = (rel % (3 * he_er_rates_per_mcs)) / he_er_rates_per_mcs;
		rate->vht.nss = 0;
	}

	if (rc.bw_tx > QCOM_WIFI_RATE_BW_MAX) {
		return -ENOTSUP;
	}

	*cfg = (uint16_t)rc.value;

	return 0;
}

static const char *qcom_wifi_rate_format_name(uint32_t format_mod)
{
	switch (format_mod) {
	case FORMATMOD_NON_HT:
		return "NON_HT";
	case FORMATMOD_NON_HT_DUP_OFDM:
		return "NON_HT_DUP_OFDM";
	case FORMATMOD_HT_MF:
		return "HT_MF";
	case FORMATMOD_HT_GF:
		return "HT_GF";
	case FORMATMOD_VHT:
		return "VHT";
	case FORMATMOD_HE_SU:
		return "HE_SU";
	case FORMATMOD_HE_MU:
		return "HE_MU";
	case FORMATMOD_HE_ER:
		return "HE_ER";
	default:
		return "unknown";
	}
}

/*
 * gi_and_pre_type_tx is overloaded per format, and nothing below this shell
 * validates it. Measured on hardware: an HT rate carrying the HE-only selector 2
 * (cfg 0x1407) was accepted, then dropped the link and left it stuck in
 * ASSOCIATING until the chip was reset. So the combination has to be rejected
 * here.
 *
 *   NON_HT    0 = long preamble, 2 = short preamble
 *   HT, VHT   0 = long GI (0.8 us), 1 = short GI (0.4 us)
 *   HE        0 = 0.8 us 2xLTF, 1 = 1.6 us 2xLTF, 2 = 3.2 us 4xLTF
 */
static const char *qcom_wifi_rate_gi_allowed(uint32_t format_mod)
{
	switch (format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		return "0 (long preamble) or 2 (short preamble)";
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
	case FORMATMOD_VHT:
		return "0 (long GI) or 1 (short GI)";
	default:
		return "0, 1 or 2";
	}
}

static int qcom_wifi_rate_apply_gi(uint16_t *cfg, uint8_t gi)
{
	union fhost_rc_rate_ctrl_info rc;

	rc.value = *cfg;

	switch (rc.format_mod_tx) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		if (gi != 0U && gi != 2U) {
			return -EINVAL;
		}
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
	case FORMATMOD_VHT:
		if (gi > 1U) {
			return -EINVAL;
		}
		break;
	default:
		if (gi > QCOM_WIFI_RATE_GI_MAX) {
			return -EINVAL;
		}
		break;
	}

	rc.gi_and_pre_type_tx = gi;
	*cfg = (uint16_t)rc.value;

	return 0;
}

/*
 * Decode the packed word back into something readable. mcs_idx carries the
 * stream count for HT/VHT/HE, so it is only a plain MCS number for the formats
 * where nss is known to be zero; print the raw field as well rather than guess.
 */
static void qcom_wifi_print_rate_cfg(const struct shell *sh, uint16_t cfg)
{
	union fhost_rc_rate_ctrl_info rc;
	static const char *const bw_names[] = { "20 MHz", "40 MHz", "80 MHz", "160 MHz" };

	rc.value = cfg;

	shell_print(sh, "%-16s 0x%04x", "rate cfg", cfg);
	shell_print(sh, "%-16s %s", "format", qcom_wifi_rate_format_name(rc.format_mod_tx));
	shell_print(sh, "%-16s %u", "mcs_idx", rc.mcs_idx);
	shell_print(sh, "%-16s %s", "bandwidth", bw_names[rc.bw_tx]);
	shell_print(sh, "%-16s %u", "gi/preamble", rc.gi_and_pre_type_tx);
	shell_print(sh, "%-16s %s", "dcm", rc.dcm_tx != 0U ? "on" : "off");
}

/*
 * The fhost stack exposes no getter for the fixed rate: wifi_mgmr_rate_config()
 * is write-only and the RC statistics behind CFGRWNX_RC_CMD are not wrapped by
 * any wifi_mgmr call. get_rate therefore reports what this shell last
 * programmed, which is not the same as what the link is really transmitting at:
 * it does not see rates set through `soc_wifi rc`, and a request the firmware
 * silently ignored (legacy OFDM) still shows up here.
 */
static uint16_t qcom_wifi_fixed_rate_cfg = QCOM_WIFI_RATE_AUTO;
static long qcom_wifi_fixed_rate_idx = -1;

static int cmd_qcom_wifi_get_rate(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (qcom_wifi_fixed_rate_cfg == QCOM_WIFI_RATE_AUTO) {
		shell_print(sh, "%-16s automatic (no fixed rate set by this shell)", "rate");
		return 0;
	}

	shell_print(sh, "%-16s %ld", "rate index", qcom_wifi_fixed_rate_idx);
	qcom_wifi_print_rate_cfg(sh, qcom_wifi_fixed_rate_cfg);

	return 0;
}

static int cmd_qcom_wifi_set_rate(const struct shell *sh, size_t argc, char **argv)
{
	const int tot_rates = TOT_RATES;
	union fhost_rc_rate_ctrl_info rc;
	uint16_t cfg;
	long idx = -1;
	int err = 0;
	int ret;

	if (strcmp(argv[1], "auto") == 0 || strcmp(argv[1], "off") == 0) {
		cfg = QCOM_WIFI_RATE_AUTO;
	} else {
		idx = shell_strtol(argv[1], 0, &err);
		if (err != 0 || idx < -1 || idx >= tot_rates) {
			shell_error(sh, "Expected <idx> in 0..%d, -1 or auto, got '%s'",
				    tot_rates - 1, argv[1]);
			return -EINVAL;
		}

		ret = qcom_wifi_rate_idx_to_cfg((int)idx, &cfg);
		if (ret != 0) {
			shell_error(sh, "Rate index %ld is not supported on this part (%d)",
				    idx, ret);
			return ret;
		}

		/*
		 * The GI override is checked against the format the index decodes
		 * to, so it has to come after the conversion.
		 */
		if (argc == 3 && cfg != QCOM_WIFI_RATE_AUTO) {
			long gi_arg = shell_strtol(argv[2], 10, &err);

			if (err != 0 || gi_arg < 0 || gi_arg > QCOM_WIFI_RATE_GI_MAX) {
				shell_error(sh, "Expected <gi> in 0..%d, got '%s'",
					    QCOM_WIFI_RATE_GI_MAX, argv[2]);
				return -EINVAL;
			}

			if (qcom_wifi_rate_apply_gi(&cfg, (uint8_t)gi_arg) != 0) {
				rc.value = cfg;
				shell_error(sh, "GI %ld is not valid for %s; expected %s",
					    gi_arg,
					    qcom_wifi_rate_format_name(rc.format_mod_tx),
					    qcom_wifi_rate_gi_allowed(rc.format_mod_tx));
				return -EINVAL;
			}
		}
	}

	ret = wifi_mgmr_rate_config(cfg);
	if (ret != 0) {
		shell_error(sh, "Unable to set rate config 0x%04x (%d)", cfg, ret);
		return -EIO;
	}

	qcom_wifi_fixed_rate_cfg = cfg;
	qcom_wifi_fixed_rate_idx = idx;

	if (cfg == QCOM_WIFI_RATE_AUTO) {
		shell_print(sh, "Fixed rate disabled");
		return 0;
	}

	shell_print(sh, "Fixed rate index %ld set (cfg 0x%04x)", idx, cfg);

	rc.value = cfg;
	if (rc.format_mod_tx == FORMATMOD_NON_HT &&
	    rc.mcs_idx >= QCOM_WIFI_RATE_FIRST_OFDM_MCS_IDX) {
		shell_warn(sh, "Legacy OFDM is accepted but not applied by the firmware; "
			       "the link keeps its previous rate.");
	}

	return 0;
}

static int cmd_qcom_wifi_set_retry_limit(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long limit;
	int err = 0;
	int ret;

	ARG_UNUSED(argc);

	limit = shell_strtoul(argv[1], 10, &err);
	if (err != 0 || limit == 0UL || limit > UINT8_MAX) {
		shell_error(sh, "Expected <retry_limit> in 1..255");
		return -EINVAL;
	}

	ret = wifi_mgmr_sta_ap_retry_limit_set((uint8_t)limit);
	if (ret != 0) {
		shell_error(sh, "Unable to set retry limit (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "Retry limit set to %lu", limit);

	return 0;
}

static int cmd_qcom_wifi_get_retry_limit(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Retry limit %u",
		    wifi_mgmr_sta_ap_retry_limit_get() & QCOM_WIFI_RETRY_LIMIT_MASK);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Country / scan list                                                       */
/* ------------------------------------------------------------------------- */

struct qcom_wifi_scanlist_ctx {
	const struct shell *sh;
	uint32_t now;
	uint32_t idx;
};

static void qcom_wifi_scanlist_row(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
	struct qcom_wifi_scanlist_ctx *ctx = arg;

	ARG_UNUSED(env);

	shell_print(ctx->sh,
		    "%3u %4u %02X:%02X:%02X:%02X:%02X:%02X %4d %3d:%-3d %3u %3u %-6s "
		    "%-17s %-9s %9u %.*s",
		    ctx->idx++, item->channel, item->bssid[0], item->bssid[1], item->bssid[2],
		    item->bssid[3], item->bssid[4], item->bssid[5], item->rssi, item->ppm_abs,
		    item->ppm_rel, item->wps, item->best_antenna,
		    wifi_mgmr_mode_to_str(item->mode), wifi_mgmr_auth_to_str(item->auth),
		    wifi_mgmr_cipher_to_str(item->cipher), ctx->now - item->timestamp_lastseen,
		    item->ssid_len, item->ssid);
}

/*
 * wifi_mgmr_sta_scanlist() is not used: it writes the entries straight to the
 * console rather than through the shell, so anything printed here interleaves
 * with it mid-line. Iterating with wifi_mgmr_scan_ap_all() keeps the header
 * attached to its rows, needs no result buffer at all, and exposes
 * best_antenna and timestamp_lastseen, which that dump omits.
 */
static int cmd_qcom_wifi_scanlist(const struct shell *sh, size_t argc, char **argv)
{
	struct qcom_wifi_scanlist_ctx ctx = {
		.sh = sh,
		/* timestamp_lastseen shares the k_uptime_get() base, measured on hardware. */
		.now = k_uptime_get_32(),
		.idx = 0U,
	};
	uint32_t nums;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	nums = wifi_mgmr_sta_scanlist_nums_get();
	shell_print(sh, "cached scan results: %u", nums);

	if (nums == 0U) {
		shell_print(sh, "cache is empty; run 'wifi scan' and read back within ~20s");
		return 0;
	}

	shell_print(sh, "idx chan bssid             rssi ppm a:r wps ant mode   auth"
			"              cipher     age(ms) ssid");

	/* Passed as both env and arg: which slot the blob forwards is undocumented. */
	return wifi_mgmr_scan_ap_all(&ctx, &ctx, qcom_wifi_scanlist_row) == 0 ? 0 : -EIO;
}

/* ------------------------------------------------------------------------- */
/* STA tunables                                                              */
/* ------------------------------------------------------------------------- */

static int cmd_qcom_wifi_keepalive(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long seconds;
	int err = 0;
	int ret;

	ARG_UNUSED(argc);

	seconds = shell_strtoul(argv[1], 10, &err);
	if (err != 0 || seconds > UINT8_MAX) {
		shell_error(sh, "Expected <seconds> in 0..255");
		return -EINVAL;
	}

	ret = wifi_mgmr_sta_keepalive_time_set((uint8_t)seconds);
	if (ret != 0) {
		shell_error(sh, "Unable to set keepalive time (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "Keepalive time set to %lu s", seconds);

	return 0;
}

static int cmd_qcom_wifi_rf_pwr(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "on") == 0) {
		ret = wifi_mgmr_rf_pwr_on();
	} else if (strcmp(argv[1], "off") == 0) {
		ret = wifi_mgmr_rf_pwr_off();
	} else if (strcmp(argv[1], "check") == 0) {
		shell_print(sh, "RF power state %d", wifi_mgmr_rf_pwr_check());
		return 0;
	} else {
		shell_error(sh, "Expected on|off|check");
		return -EINVAL;
	}

	if (ret != 0) {
		shell_error(sh, "RF power %s failed (%d)", argv[1], ret);
		return -EIO;
	}

	shell_print(sh, "RF power %s", argv[1]);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* PMK cache                                                                 */
/* ------------------------------------------------------------------------- */

static int cmd_qcom_wifi_pmk_read(const struct shell *sh, size_t argc, char **argv)
{
	wifi_mgmr_pmk_cache_entry entry;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memset(&entry, 0, sizeof(entry));

	ret = wifi_mgmr_pmk_cache_entry_read(&entry);
	if (ret != 0) {
		shell_print(sh, "No PMK cache entry (%d)", ret);
		return 0;
	}

	shell_print(sh, "PMK cache entry present");
	shell_hexdump(sh, (const uint8_t *)&entry, sizeof(entry));

	return 0;
}

/* ------------------------------------------------------------------------- */
/* SoftAP                                                                    */
/* ------------------------------------------------------------------------- */

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
static int cmd_qcom_wifi_ap_bcn_mode(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long mode;
	long timer;
	int err = 0;
	int ret;

	if (argc == 1) {
		uint8_t bcn_mode = 0U;
		int bcn_timer = 0;

		wifi_mgmr_ap_bcn_mode_get(&bcn_mode, &bcn_timer);
		shell_print(sh, "bcn mode %u, timer %d", bcn_mode, bcn_timer);
		return 0;
	}

	/* SHELL_CMD_ARG() allows argc 1..3, so reject the half-supplied form
	 * before argv[2] (NULL past argc) reaches shell_strtol().
	 */
	if (argc != 3) {
		shell_error(sh, "Both <mode> and <timer> are required to set");
		return -EINVAL;
	}

	mode = shell_strtoul(argv[1], 10, &err);
	if (err != 0 || mode > UINT8_MAX) {
		shell_error(sh, "Unable to parse <mode>");
		return -EINVAL;
	}

	timer = shell_strtol(argv[2], 10, &err);
	if (err != 0) {
		shell_error(sh, "Unable to parse <timer>");
		return -EINVAL;
	}

	ret = wifi_mgmr_ap_bcn_mode_set((uint8_t)mode, (int)timer);
	if (ret != 0) {
		shell_error(sh, "Unable to set beacon mode (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "bcn mode set to %lu, timer %ld", mode, timer);

	return 0;
}

static int cmd_qcom_wifi_ap_acl_mode(const struct shell *sh, size_t argc, char **argv)
{
	ap_acl_prem prem;
	int ret;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "off") == 0) {
		prem = DISABLE_ACL;
	} else if (strcmp(argv[1], "accept") == 0) {
		prem = ACCEPT_UNLESS_DENIED;
	} else if (strcmp(argv[1], "deny") == 0) {
		prem = DENY_UNLESS_ACCEPTED;
	} else {
		shell_error(sh, "Expected off|accept|deny");
		return -EINVAL;
	}

	ret = wifi_mgmr_ap_acl_enable(prem);
	if (ret != 0) {
		shell_error(sh, "Unable to set ACL mode (%d)", ret);
		return -EIO;
	}

	shell_print(sh, "ACL default policy set to %s", argv[1]);

	return 0;
}

static int qcom_wifi_acl_action(const struct shell *sh, const char *list, ap_action_type action,
				char *mac_str)
{
	ap_acl_type acl;
	int ret;

	if (strcmp(list, "accept") == 0) {
		acl = ACCEPT_ACL;
	} else if (strcmp(list, "deny") == 0) {
		acl = DENY_ACL;
	} else {
		shell_error(sh, "Expected list accept|deny");
		return -EINVAL;
	}

	ret = wifi_mgmr_ap_acl_set(acl, action, mac_str);
	if (ret != 0) {
		shell_error(sh, "ACL operation failed (%d)", ret);
		return -EIO;
	}

	return 0;
}

static int cmd_qcom_wifi_ap_acl_add(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return qcom_wifi_acl_action(sh, argv[1], ADD_ACL, argv[2]);
}

static int cmd_qcom_wifi_ap_acl_del(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return qcom_wifi_acl_action(sh, argv[1], DELETE_ACL, argv[2]);
}

static int cmd_qcom_wifi_ap_acl_show(const struct shell *sh, size_t argc, char **argv)
{
	/* SHOW/CLEAR ignore the address, but the callee is a blob: hand it an
	 * empty string rather than NULL so it cannot fault on a bare strlen().
	 */
	char unused[1] = { '\0' };

	ARG_UNUSED(argc);

	return qcom_wifi_acl_action(sh, argv[1], SHOW_ACL, unused);
}

static int cmd_qcom_wifi_ap_acl_clear(const struct shell *sh, size_t argc, char **argv)
{
	char unused[1] = { '\0' };

	ARG_UNUSED(argc);

	return qcom_wifi_acl_action(sh, argv[1], CLEAR_ACL, unused);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qcom_wifi_ap_acl,
			       SHELL_CMD_ARG(mode, NULL,
					     "Set the ACL default policy.\n"
					     "Usage: qcom wifi ap acl mode <off|accept|deny>",
					     cmd_qcom_wifi_ap_acl_mode, 2, 0),
			       SHELL_CMD_ARG(add, NULL,
					     "Add a MAC to an ACL.\n"
					     "Usage: qcom wifi ap acl add <accept|deny> "
					     "<xx:xx:xx:xx:xx:xx>",
					     cmd_qcom_wifi_ap_acl_add, 3, 0),
			       SHELL_CMD_ARG(del, NULL,
					     "Remove a MAC from an ACL.\n"
					     "Usage: qcom wifi ap acl del <accept|deny> "
					     "<xx:xx:xx:xx:xx:xx>",
					     cmd_qcom_wifi_ap_acl_del, 3, 0),
			       SHELL_CMD_ARG(show, NULL,
					     "Dump an ACL.\n"
					     "Usage: qcom wifi ap acl show <accept|deny>",
					     cmd_qcom_wifi_ap_acl_show, 2, 0),
			       SHELL_CMD_ARG(clear, NULL,
					     "Clear an ACL.\n"
					     "Usage: qcom wifi ap acl clear <accept|deny>",
					     cmd_qcom_wifi_ap_acl_clear, 2, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qcom_wifi_ap,
			       SHELL_CMD_ARG(bcn_mode, NULL,
					     "Get or set the beacon transmit mode.\n"
					     "Usage: qcom wifi ap bcn_mode [<mode> <timer>]",
					     cmd_qcom_wifi_ap_bcn_mode, 1, 2),
			       SHELL_CMD(acl, &sub_qcom_wifi_ap_acl, "MAC access control lists.",
					 NULL),
			       SHELL_SUBCMD_SET_END);
#endif /* CONFIG_QC7XX_WIFI_AP_STA_MODE */

/* ------------------------------------------------------------------------- */
/* Command tree                                                              */
/* ------------------------------------------------------------------------- */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qcom_wifi_pmk,
			       SHELL_CMD_ARG(read, NULL,
					     "Dump the persisted PMK cache entry.\n"
					     "Prints key material in the clear; debug use only.",
					     cmd_qcom_wifi_pmk_read, 1, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qcom_wifi_commands,
			       SHELL_CMD_ARG(info, NULL, "Show the WiFi device summary.",
					     cmd_qcom_wifi_info, 1, 0),
			       SHELL_CMD_ARG(set_tx_power, NULL,
					     "Set the transmit power.\n"
					     "Usage: qcom wifi set_tx_power <power_dBm|default>\n"
					     "<power_dBm>: exact tx power in dBm, 0..20.\n"
					     "  A .5 fraction is accepted (e.g. 12.5); the\n"
					     "  hardware step is 0.5dBm.\n"
					     "'default': drop the explicit power. Probe only,\n"
					     "  verify the result with get_tx_power.\n"
					     "The value is written through unclamped: this\n"
					     "stack has no regulatory power policy.\n",
					     cmd_qcom_wifi_set_tx_power, 2, 0),
			       SHELL_CMD_ARG(get_tx_power, NULL, "Show the transmit power.",
					     cmd_qcom_wifi_get_tx_power, 1, 0),
			       SHELL_CMD_ARG(tpc_show, NULL, "Dump the per-rate power table.",
					     cmd_qcom_wifi_tpc_show, 1, 0),
			       SHELL_CMD_ARG(set_phy_mode, NULL,
					     "Set the PHY mode.\n"
					     "Usage: qcom wifi set_phy_mode <sta|ap> "
					     "<b|bg|bgn|bgnax|0xNNN>\n"
					     "sta and ap are set independently.\n"
					     "Only these four modes work: b(0x1),\n"
					     "bg(0x5), bgn(0xd), bgnax(0x8d). A raw\n"
					     "0xNNN must be a contiguous subset from\n"
					     "11b up, so 11ax needs 11n; anything\n"
					     "else, plus 11a and 11ac, is refused by\n"
					     "the stack.",
					     cmd_qcom_wifi_set_phy_mode, 3, 0),
			       SHELL_CMD_ARG(get_phy_mode, NULL,
					     "Show the PHY mode.\n"
					     "Usage: qcom wifi get_phy_mode [<sta|ap>]",
					     cmd_qcom_wifi_get_phy_mode, 1, 1),
			       SHELL_CMD_ARG(set_rate, NULL,
					     "Pin the transmit rate of the connected link.\n"
					     "Usage: qcom wifi set_rate <auto|-1|idx> [<gi>]\n"
					     "<idx>: rate-control table index, from the 20M and\n"
					     "       40M sheets of the vendor rate table.\n"
					     "       1/3/5/7 CCK,\n"
					     "       8-15 legacy OFDM, 16-46 HT, 144-210 VHT,\n"
					     "       785-920 HE SU, 3665-3686 HE ER. Indices\n"
					     "       above 40 MHz are rejected.\n"
					     "<gi>:  GI override, meaning depends on format.\n"
					     "       HE:     0 = 0.8us 2xLTF,\n"
					     "               1 = 1.6us 2xLTF,\n"
					     "               2 = 3.2us 4xLTF\n"
					     "       HT/VHT: 0 = long GI, 1 = short GI\n"
					     "       CCK:    0 = long, 2 = short preamble\n"
					     "auto or -1 restores automatic rate control.",
					     cmd_qcom_wifi_set_rate, 2, 1),
			       SHELL_CMD_ARG(get_rate, NULL,
					     "Show the fixed rate last set by set_rate.\n"
					     "Usage: qcom wifi get_rate\n"
					     "The stack has no getter, so this is the shell's\n"
					     "own record: it does not see rates set through\n"
					     "soc_wifi rc, nor requests the firmware ignored.",
					     cmd_qcom_wifi_get_rate, 1, 0),
			       SHELL_CMD_ARG(set_retry_limit, NULL,
					     "Set the transmit retry limit.\n"
					     "Usage: qcom wifi set_retry_limit <1-255>",
					     cmd_qcom_wifi_set_retry_limit, 2, 0),
			       SHELL_CMD_ARG(get_retry_limit, NULL,
					     "Show the transmit retry limit.",
					     cmd_qcom_wifi_get_retry_limit, 1, 0),
			       SHELL_CMD_ARG(scanlist, NULL, "Dump the cached scan results.",
					     cmd_qcom_wifi_scanlist, 1, 0),
			       SHELL_CMD_ARG(remaining_tx, NULL,
					     "Show the pending frame count per transmit queue.",
					     cmd_qcom_wifi_remaining_tx, 1, 0),
			       SHELL_CMD_ARG(keepalive, NULL,
					     "Set the STA keepalive period.\n"
					     "Usage: qcom wifi keepalive <seconds>",
					     cmd_qcom_wifi_keepalive, 2, 0),
			       SHELL_CMD_ARG(rf_pwr, NULL,
					     "Control the RF power domain.\n"
					     "Usage: qcom wifi rf_pwr <on|off|check>",
					     cmd_qcom_wifi_rf_pwr, 2, 0),
			       SHELL_CMD(pmk, &sub_qcom_wifi_pmk, "PMK cache control.", NULL),
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
			       SHELL_CMD(ap, &sub_qcom_wifi_ap, "SoftAP control.", NULL),
#endif
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((qcom), wifi, &sub_qcom_wifi_commands, "wifi commands", NULL, 1, 0);
