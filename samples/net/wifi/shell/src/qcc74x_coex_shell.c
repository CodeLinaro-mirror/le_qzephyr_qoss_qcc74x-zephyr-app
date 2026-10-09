/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/uart.h>

#include "wifi_mgmr_ext.h"

#if defined(CONFIG_BT_OBSERVER)
#define COEX_SCAN_MAX_UNIQUE 64

static atomic_t coex_scan_packets;
static atomic_t coex_scan_unique;
static atomic_t coex_scan_last_ms;
static bt_addr_le_t coex_scan_addresses[COEX_SCAN_MAX_UNIQUE];
static int coex_scan_previous_packets;
static int coex_scan_sequence;
static K_MUTEX_DEFINE(coex_scan_stats_lock);

static void coex_scan_recv(const struct bt_le_scan_recv_info *info,
			   struct net_buf_simple *buf)
{
	ARG_UNUSED(buf);
	unsigned int key = irq_lock();
	bool known = false;
	int unique = atomic_get(&coex_scan_unique);

	for (int i = 0; i < unique; i++) {
		if (bt_addr_le_cmp(&coex_scan_addresses[i], info->addr) == 0) {
			known = true;
			break;
		}
	}
	if (!known && unique < COEX_SCAN_MAX_UNIQUE) {
		bt_addr_le_copy(&coex_scan_addresses[unique], info->addr);
		atomic_set(&coex_scan_unique, unique + 1);
	}
	atomic_inc(&coex_scan_packets);
	atomic_set(&coex_scan_last_ms, (atomic_val_t)k_uptime_get_32());
	irq_unlock(key);
}

static struct bt_le_scan_cb coex_scan_cb = {
	.recv = coex_scan_recv,
};

/*
 * Scan counters are reported only when explicitly queried.  An unsolicited
 * periodic print would share the console with the shell and could land in the
 * middle of a command being typed, splitting its echo and corrupting the
 * transaction the host is waiting on.
 */
static void coex_scan_stats_report(const struct shell *sh)
{
	uint32_t now = k_uptime_get_32();
	uint32_t last = (uint32_t)atomic_get(&coex_scan_last_ms);
	int packets = atomic_get(&coex_scan_packets);
	int unique = atomic_get(&coex_scan_unique);

	shell_print(sh, "COEX_BLE_SCAN_STATS seq=%d packets=%d unique=%d "
		    "interval_packets=%d last_ms=%u",
		    ++coex_scan_sequence, packets, unique,
		    packets - coex_scan_previous_packets,
		    (uint32_t)(now - last));
	coex_scan_previous_packets = packets;
}

static int coex_scan_stats_init(void)
{
	#if defined(CONFIG_WIFI_SHELL_COEX_VALIDATION)
	char identity[320];
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	int length = snprintk(identity, sizeof(identity),
	       "\r\nCOEX_FW identity=wifi-shell-coex protocol=1 build=%s "
	       "central=%d observer=%d gatt_client=%d bt_shell=%d max_conn=%d "
	       "coex=%d low_power=%d END\r\n",
	       STRINGIFY(BUILD_VERSION), IS_ENABLED(CONFIG_BT_CENTRAL),
	       IS_ENABLED(CONFIG_BT_OBSERVER), IS_ENABLED(CONFIG_BT_GATT_CLIENT),
	       IS_ENABLED(CONFIG_BT_SHELL), COND_CODE_1(CONFIG_BT, (CONFIG_BT_MAX_CONN), (0)),
	       IS_ENABLED(CONFIG_QCC74X_WIFI_COEX), IS_ENABLED(CONFIG_QCC74X_WIFI_LOW_POWER));
	/* Identity must not depend on printk's log-backend filtering/routing. */
	if (device_is_ready(console) && length > 0 && length < sizeof(identity)) {
		for (int i = 0; i < length; i++) {
			uart_poll_out(console, identity[i]);
		}
	}
#endif
	return bt_le_scan_cb_register(&coex_scan_cb);
}

SYS_INIT(coex_scan_stats_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

static int cmd_coex_enable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = wifi_mgmr_sta_coex_enable();

	shell_print(sh, "qcc74x_coex enable: rc=%d", rc);
	return rc;
}

static int cmd_coex_disable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = wifi_mgmr_sta_coex_disable();

	shell_print(sh, "qcc74x_coex disable: rc=%d", rc);
	return rc;
}

static int cmd_coex_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int status = wifi_mgmr_sta_coex_status_get();
	int duty_ms = wifi_mgmr_sta_coex_duty_get();

	shell_print(sh, "qcc74x_coex: status=%d duty_ms=%d", status, duty_ms);
	return status < 0 ? status : 0;
}

static int cmd_coex_duty(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		int duty_ms = wifi_mgmr_sta_coex_duty_get();

		shell_print(sh, "qcc74x_coex duty: %d ms", duty_ms);
		return duty_ms < 0 ? duty_ms : 0;
	}

	char *endptr;
	long duty_ms = strtol(argv[1], &endptr, 0);

	if (*endptr != '\0' || duty_ms < 10 || duty_ms > 90) {
		shell_error(sh, "duty range: 10..90 ms");
		return -EINVAL;
	}

	int rc = wifi_mgmr_sta_coex_duty_set((uint8_t)duty_ms);

	shell_print(sh, "qcc74x_coex duty set: %ld ms rc=%d", duty_ms, rc);
	return rc;
}

#if defined(CONFIG_BT_OBSERVER)
static int cmd_coex_scanstats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&coex_scan_stats_lock, K_FOREVER);
	coex_scan_stats_report(sh);
	k_mutex_unlock(&coex_scan_stats_lock);
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(qcc74x_coex_cmds,
	SHELL_CMD_ARG(enable, NULL, NULL, cmd_coex_enable, 1, 0),
	SHELL_CMD_ARG(disable, NULL, NULL, cmd_coex_disable, 1, 0),
	SHELL_CMD_ARG(status, NULL, NULL, cmd_coex_status, 1, 0),
	SHELL_CMD_ARG(duty, NULL, NULL, cmd_coex_duty, 1, 1),
#if defined(CONFIG_BT_OBSERVER)
	SHELL_CMD_ARG(scanstats, NULL, "Report BLE scan counters", cmd_coex_scanstats, 1, 0),
#endif
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((qcom), coex, &qcc74x_coex_cmds, "coex commands", NULL, 1, 0);
