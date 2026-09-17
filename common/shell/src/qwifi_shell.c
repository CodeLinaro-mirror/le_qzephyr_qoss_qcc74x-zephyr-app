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

static int cmd_qcom_wifi_set_rate(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long cfg;
	int err = 0;
	int ret;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "auto") == 0 || strcmp(argv[1], "off") == 0) {
		cfg = 0xFFFFUL;
	} else {
		cfg = shell_strtoul(argv[1], 0, &err);
		if (err != 0 || cfg > 0xFFFFUL) {
			shell_error(sh, "Unable to parse <rate_cfg> '%s'", argv[1]);
			return -EINVAL;
		}
	}

	ret = wifi_mgmr_rate_config((uint16_t)cfg);
	if (ret != 0) {
		shell_error(sh, "Unable to set rate config 0x%04lx (%d)", cfg, ret);
		return -EIO;
	}

	if (cfg == 0xFFFFUL) {
		shell_print(sh, "Fixed rate disabled");
	} else {
		shell_print(sh, "Fixed rate config set to 0x%04lx", cfg);
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
					     "Pin the transmit rate.\n"
					     "Usage: qcom wifi set_rate <auto|rate_cfg>\n"
					     "<rate_cfg>: fhost fixed rate config word.\n",
					     cmd_qcom_wifi_set_rate, 2, 0),
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
