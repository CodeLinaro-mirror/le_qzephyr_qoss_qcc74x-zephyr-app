/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/shell/shell.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_diag.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_diag.h>

#include <qc7xx_lp.h>

#if defined(CONFIG_WIFI_SHELL_LOWPOWER_VALIDATION)
static uint64_t lp_pds_ms_to_us(uint32_t ms)
{
	return (uint64_t)ms * USEC_PER_MSEC;
}

static uint64_t lp_pds_ratio_pct_x100(uint64_t part_us, uint64_t total_us)
{
	if ((total_us == 0U) || (part_us == 0U)) {
		return 0U;
	}

	return (part_us * 10000ULL) / total_us;
}

static const char *lp_pds_deep_skip_reason_name(uint32_t reason)
{
	switch (reason) {
	case QCC74X_PM_DEEP_SKIP_REASON_NOT_ARMED:
		return "not_armed";
	case QCC74X_PM_DEEP_SKIP_REASON_ENTER_BUSY:
		return "enter_busy";
	case QCC74X_PM_DEEP_SKIP_REASON_DEADLINE_REACHED:
		return "deadline_reached";
	case QCC74X_PM_DEEP_SKIP_REASON_NEXT_TIMEOUT_IMMEDIATE:
		return "next_timeout_immediate";
	case QCC74X_PM_DEEP_SKIP_REASON_NO_WAKE_SOURCE:
		return "no_wake_source";
	case QCC74X_PM_DEEP_SKIP_REASON_WIFI_BLOCKED:
		return "wifi_blocked";
	case QCC74X_PM_DEEP_SKIP_REASON_BLE_BLOCKED:
		return "ble_blocked";
	default:
		return "none";
	}
}

static const char *lp_pds_wifi_diag_reason_name(uint32_t reason)
{
	switch (reason) {
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_WIFI_DISABLED:
		return "wifi_disabled";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_NOT_CONNECTED:
		return "not_connected";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_NOT_COMPILED:
		return "ps_not_compiled";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_ACTIVE_CHECK_NOT_RUN:
		return "active_check_not_run";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_SLEEP_CHECK_FAILED:
		return "ps_sleep_check_failed";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_DTIM_WINDOW_BLOCKED:
		return "dtim_window_blocked";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_TOO_CLOSE:
		return "twt_too_close";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_READY:
		return "ready";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_STA_BUSY:
		return "sta_busy";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE:
	default:
		return "none";
	}
}

static const char *lp_pds_auto_diag_reason_name(uint32_t reason)
{
	switch (reason) {
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_AUTO_DISABLED:
		return "auto_disabled";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_NOT_INITIALIZED:
		return "not_initialized";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_STATE_LOCKED:
		return "state_locked";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_TIMEOUT_TOO_SHORT:
		return "timeout_too_short";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_ENTER_BUSY:
		return "enter_busy";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_RESUME_PENDING:
		return "resume_pending";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_WIFI_NOT_READY:
		return "wifi_not_ready";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_READY:
		return "ready";
	case QCC74X_PM_PDS15_AUTO_DIAG_REASON_NONE:
	default:
		return "none";
	}
}

static int lp_pds_parse_u32(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	if ((text == NULL) || (value == NULL)) {
		return -EINVAL;
	}

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if ((errno != 0) || (end == text) || (*end != '\0') ||
	    (parsed > UINT32_MAX)) {
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static uint64_t lp_pds_auto_remaining_us(
	const struct qcc74x_pm_pds15_status *status)
{
	uint64_t now_us;

	if ((status == NULL) || !status->auto_deadline_active ||
	    (status->auto_deadline_us == 0U)) {
		return 0U;
	}

	now_us = qc7xx_lp_get_virtual_us();
	if (now_us >= status->auto_deadline_us) {
		return 0U;
	}

	return status->auto_deadline_us - now_us;
}

static void lp_pds_print_diag(const struct shell *sh,
			      const struct qcc74x_pm_diag_stats *diag)
{
	shell_print(sh,
		    "lp_pds diag: state_set=%u exit_post_ops=%u shallow=%u deep=%u prepare=%u skip=%u last_skip=%s resume_ok=%u resume_fail=%u last_rc=%d wake_reason=0x%08x uart_resume_rc=%d requested_us=%llu elapsed_us=%llu",
		    diag->state_set_calls,
		    diag->exit_post_ops_calls,
		    diag->shallow_idle_calls,
		    diag->deep_sleep_calls,
		    diag->deep_prepare_calls,
		    diag->deep_skip_calls,
		    lp_pds_deep_skip_reason_name(
			    diag->last_deep_skip_reason),
		    diag->deep_resume_success,
		    diag->deep_resume_failures,
		    diag->last_deep_return_code,
		    diag->last_wake_reason,
		    diag->last_uart_resume_rc,
		    (unsigned long long)diag->last_deep_requested_us,
		    (unsigned long long)diag->last_deep_elapsed_us);
	shell_print(
		sh,
		"lp_pds diag_skip: not_armed=%u enter_busy=%u deadline_reached=%u next_timeout_immediate=%u no_wake_source=%u wifi_blocked=%u ble_blocked=%u",
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_NOT_ARMED],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_ENTER_BUSY],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_DEADLINE_REACHED],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_NEXT_TIMEOUT_IMMEDIATE],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_NO_WAKE_SOURCE],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_WIFI_BLOCKED],
		diag->deep_skip_reason_counts
			[QCC74X_PM_DEEP_SKIP_REASON_BLE_BLOCKED]);
}

static void lp_pds_print_power_stats(const struct shell *sh)
{
	qc7xx_lp_info_t info;
	struct qcc74x_pm_pds15_status status;
	uint64_t sleep_pct_x100;
	uint64_t lpfw_pct_x100;
	uint64_t app_pct_x100;

	memset(&status, 0, sizeof(status));
	qcc74x_pm_pds15_get_status(&status);
	if (!status.initialized) {
		shell_warn(sh, "lp_pds power: LPFW not initialized yet");
		return;
	}

	memset(&info, 0, sizeof(info));
	qc7xx_lp_info_get(&info);

	sleep_pct_x100 = lp_pds_ratio_pct_x100(info.sleep_pds_us,
					       info.time_total_us);
	lpfw_pct_x100 = lp_pds_ratio_pct_x100(info.active_lpfw_us,
					      info.time_total_us);
	app_pct_x100 = lp_pds_ratio_pct_x100(info.active_app_us,
					     info.time_total_us);

	shell_print(
		sh,
		"lp_pds power: total_us=%llu sleep_pds_us=%llu active_lpfw_us=%llu active_app_us=%llu sleep_pct=%llu.%02llu lpfw_pct=%llu.%02llu app_pct=%llu.%02llu lpfw_wakeup_cnt=%d recv_cnt=%u loss_cnt=%u",
		(unsigned long long)info.time_total_us,
		(unsigned long long)info.sleep_pds_us,
		(unsigned long long)info.active_lpfw_us,
		(unsigned long long)info.active_app_us,
		(unsigned long long)(sleep_pct_x100 / 100ULL),
		(unsigned long long)(sleep_pct_x100 % 100ULL),
		(unsigned long long)(lpfw_pct_x100 / 100ULL),
		(unsigned long long)(lpfw_pct_x100 % 100ULL),
		(unsigned long long)(app_pct_x100 / 100ULL),
		(unsigned long long)(app_pct_x100 % 100ULL),
		info.lpfw_wakeup_cnt, info.lpfw_recv_cnt, info.lpfw_loss_cnt);
	shell_print(
		sh,
		"lp_pds power_ms: total=%llu pds=%llu lpfw=%llu app=%llu",
		(unsigned long long)(info.time_total_us / USEC_PER_MSEC),
		(unsigned long long)(info.sleep_pds_us / USEC_PER_MSEC),
		(unsigned long long)(info.active_lpfw_us / USEC_PER_MSEC),
		(unsigned long long)(info.active_app_us / USEC_PER_MSEC));

	if (info.time_total_us == 0U) {
		shell_warn(
			sh,
			"lp_pds power: total_us is 0, run a low-power session first");
	}
}

static void lp_pds_print_wifi_diag(const struct shell *sh)
{
	struct qcc74x_pm_pds15_wifi_diag_status diag;

	qcc74x_pm_pds15_wifi_diag_get(&diag);

	shell_print(sh,
		    "lp_pds wifi: sampled=%d samples=%u active_checks=%u connected_samples=%u wifi_config=%d ps_compiled=%d active_check_ran=%d",
		    diag.sampled, diag.sample_count, diag.active_check_count,
		    diag.connected_count, diag.wifi_config_enabled,
		    diag.wifi_ps_compiled, diag.active_check_ran);
	shell_print(sh,
		    "lp_pds wifi_state: connected=%d raw_state=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x bssid_rc=%d channel_rc=%d channel=%d aid=%d twt_flow=%d",
		    diag.sta_connected,
		    diag.sta_raw_state,
		    diag.bssid[0], diag.bssid[1], diag.bssid[2],
		    diag.bssid[3], diag.bssid[4], diag.bssid[5],
		    diag.bssid_rc, diag.channel_rc, diag.channel, diag.aid,
		    diag.twt_flow_count);
	shell_print(sh,
		    "lp_pds wifi_checks: ps_sleep_check=%d lpfw_check_allow=%d next_wakeup_us=%u next_valid=%d last_reason=%u(%s)",
		    diag.ps_sleep_check_rc, diag.lpfw_check_allow,
		    diag.next_wakeup_timer_us, diag.next_wakeup_timer_valid,
		    diag.last_reason,
		    lp_pds_wifi_diag_reason_name(diag.last_reason));
	shell_print(sh,
		    "lp_pds wifi_ps_detail: cpu=%d queue_sent_empty=%d queue_saved_empty=%d ps=%d tx=%d machw=%d",
		    diag.ps_cpu_can_sleep, diag.ps_queue_sent_empty,
		    diag.ps_queue_saved_empty, diag.ps_allowed,
		    diag.tx_allowed, diag.machw_allowed);

	for (uint32_t reason = 0U;
	     reason < QCC74X_PM_PDS15_WIFI_DIAG_REASON_COUNT; reason++) {
		if (diag.reason_counts[reason] == 0U) {
			continue;
		}
		shell_print(sh, "lp_pds wifi_reason[%u:%s]=%u",
			    reason, lp_pds_wifi_diag_reason_name(reason),
			    diag.reason_counts[reason]);
	}
}

static void lp_pds_print_auto_diag(const struct shell *sh)
{
	struct qcc74x_pm_pds15_auto_diag_status diag;
	uint32_t reason_count = 0U;

	qcc74x_pm_pds15_auto_diag_get(&diag);
	if (diag.last_reason < QCC74X_PM_PDS15_AUTO_DIAG_REASON_COUNT) {
		reason_count = diag.reason_counts[diag.last_reason];
	}

	shell_print(sh,
		    "lp_pds auto_diag: sampled=%d sample=%u ready=%u reason=%s reason_count=%u ticks=%d min_ticks=%u timeout_us=%llu state_available=%d deadline_active=%d deadline_expired=%d remaining_us=%llu",
		    diag.sampled,
		    diag.sample_count,
		    diag.ready_count,
		    lp_pds_auto_diag_reason_name(diag.last_reason),
		    reason_count,
		    diag.ticks,
		    diag.min_residency_ticks,
		    (unsigned long long)diag.timeout_us,
		    diag.state_available,
		    diag.auto_deadline_active,
		    diag.auto_deadline_expired,
		    (unsigned long long)diag.auto_remaining_us);
	shell_print(sh,
		    "lp_pds auto_diag_state: config=%d enabled=%d policy_lock=%d initialized=%d enter_busy=%d resume_pending=%d wifi_reason=%s",
		    diag.auto_config_enabled,
		    diag.auto_enabled,
		    diag.policy_lock_active,
		    diag.initialized,
		    diag.enter_busy,
		    diag.resume_pending,
		    lp_pds_wifi_diag_reason_name(diag.wifi_reason));
}

static void lp_pds_print_status(const struct shell *sh)
{
	struct qcc74x_pm_pds15_status status;
	struct qcc74x_pm_diag_stats diag;
	uint32_t deep_sleep_cycles;
	uint32_t deep_sleep_ms;

	qcc74x_pm_pds15_get_status(&status);
	qcc74x_pm_diag_get(&diag);
	deep_sleep_ms = (uint32_t)(status.timeout_us / USEC_PER_MSEC);
	deep_sleep_cycles = status.cycle_count;

	shell_print(sh,
		    "lp_pds config: lowpower=1 diag=%d pds15=%d uart_resume=%d psram=%d",
		    IS_ENABLED(CONFIG_QCC74X_PM_DIAGNOSTICS),
		    IS_ENABLED(CONFIG_QCC74X_PM_PDS15),
		    IS_ENABLED(CONFIG_QCC74X_PM_PDS15_UART_RESUME),
		    IS_ENABLED(CONFIG_PSRAM));
	shell_print(sh,
		    "lp_pds state: initialized=%d init_in_progress=%d init_rc=%d policy_lock=%d current_slice_us=%llu",
		    status.initialized,
		    status.init_in_progress,
		    status.init_rc,
		    qcc74x_pm_pds15_policy_lock_is_active(),
		    (unsigned long long)status.timeout_us);
	shell_print(sh,
		    "lp_pds auto: config=%d enabled=%d policy_lock=%d pending=%d timeout_us=%llu deadline_active=%d remaining_us=%llu",
		    IS_ENABLED(CONFIG_QCC74X_PM_PDS15_POLICY),
		    status.auto_enabled,
		    status.policy_lock_active,
		    status.auto_entry_pending,
		    (unsigned long long)status.auto_timeout_us,
		    status.auto_deadline_active,
		    (unsigned long long)lp_pds_auto_remaining_us(&status));
	shell_print(sh,
		    "lp_pds deep_sleep: sleep_ms=%u cycles=%u",
		    deep_sleep_ms, deep_sleep_cycles);
	lp_pds_print_power_stats(sh);
	lp_pds_print_diag(sh, &diag);
	qcc74x_pm_pds15_auto_diag_sample();
	lp_pds_print_auto_diag(sh);
	lp_pds_print_wifi_diag(sh);
}

static int cmd_lp_pds_show(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	lp_pds_print_status(sh);

	return 0;
}

static int cmd_lp_pds_auto(const struct shell *sh, size_t argc, char *argv[])
{
	struct qcc74x_pm_pds15_status status;
	uint32_t duration_ms = 0U;
	bool enable;
	bool release_pending = false;
	int rc;

	if ((argc < 2U) || (argc > 3U)) {
		shell_error(sh,
			    "lp_pds auto: usage lp_pds auto <on [duration_ms]|off|show>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "show") == 0) {
		if (argc != 2U) {
			shell_error(sh,
				    "lp_pds auto: usage lp_pds auto show");
			return -EINVAL;
		}
		qcc74x_pm_pds15_get_status(&status);
		qcc74x_pm_pds15_auto_diag_sample();
		shell_print(sh,
			    "lp_pds auto: config=%d enabled=%d policy_lock=%d initialized=%d init_rc=%d pending=%d timeout_us=%llu deadline_active=%d deadline_us=%llu remaining_us=%llu",
			    IS_ENABLED(CONFIG_QCC74X_PM_PDS15_POLICY),
			    status.auto_enabled,
			    status.policy_lock_active,
			    status.initialized,
			    status.init_rc,
			    status.auto_entry_pending,
			    (unsigned long long)status.auto_timeout_us,
			    status.auto_deadline_active,
			    (unsigned long long)status.auto_deadline_us,
			    (unsigned long long)lp_pds_auto_remaining_us(
				    &status));
		lp_pds_print_auto_diag(sh);
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		enable = true;
		if ((argc == 3U) &&
		    (lp_pds_parse_u32(argv[2], &duration_ms) != 0)) {
			shell_error(sh, "lp_pds auto: invalid duration_ms '%s'",
				    argv[2]);
			return -EINVAL;
		}
	} else if (strcmp(argv[1], "off") == 0) {
		if (argc != 2U) {
			shell_error(sh,
				    "lp_pds auto: usage lp_pds auto off");
			return -EINVAL;
		}
		enable = false;
	} else {
		shell_error(sh,
			    "lp_pds auto: usage lp_pds auto <on [duration_ms]|off|show>");
		return -EINVAL;
	}
	if (enable && (duration_ms != 0U)) {
		rc = qcc74x_pm_pds15_auto_arm_for(
			lp_pds_ms_to_us(duration_ms), &release_pending);
	} else {
		rc = qcc74x_pm_pds15_auto_enable(enable);
	}
	if (rc != 0) {
		shell_error(sh, "lp_pds auto: %s failed rc=%d",
			    enable ? "on" : "off", rc);
		return rc;
	}

	qcc74x_pm_pds15_get_status(&status);
	shell_print(sh,
		    "lp_pds auto: %s enabled=%d policy_lock=%d duration_ms=%u remaining_us=%llu",
		    enable ? "on" : "off", status.auto_enabled,
		    status.policy_lock_active,
		    status.auto_deadline_active ? duration_ms : 0U,
		    (unsigned long long)lp_pds_auto_remaining_us(&status));

	if (release_pending) {
		qcc74x_pm_pds15_auto_release_pending_lock();
	}

	return 0;
}

static int cmd_lp_pds_reset(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	(void)qcc74x_pm_pds15_auto_enable(false);
	(void)qcc74x_pm_pds15_policy_lock_set(true);
	qcc74x_pm_diag_reset();
	qcc74x_pm_pds15_wifi_diag_reset();
	qcc74x_pm_pds15_auto_diag_reset();
	qc7xx_lp_info_clear();

	shell_print(sh,
		    "lp_pds: disabled auto low power, locked PDS15, reset diagnostics, and cleared LP power stats");

	return 0;
}

static int cmd_lp_pds_wifi_show(const struct shell *sh, size_t argc,
				char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	lp_pds_print_wifi_diag(sh);

	return 0;
}

#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
static int cmd_lp_pds_ble_show(const struct shell *sh, size_t argc,
			       char *argv[])
{
	struct qcc74x_pm_pds15_ble_diag_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qcc74x_pm_pds15_ble_diag_get(&status);
	shell_print(sh,
		    "lp_pds ble: sampled=%d bt_ready=%d sample=%u allowed=%u capped=%u blocked=%u crash_recovery=%u last_sleep_rtc=%d last_capped_us=%llu last_crash_elapsed_us=%llu last_crash_expected_us=%llu",
		    status.sampled, status.bt_ready,
		    status.sample_count, status.allowed_count,
		    status.capped_count, status.blocked_count,
		    status.crash_recovery_count,
		    status.last_sleep_rtc,
		    (unsigned long long)status.last_capped_us,
		    (unsigned long long)status.last_crash_elapsed_us,
		    (unsigned long long)status.last_crash_expected_us);
	return 0;
}

static int cmd_lp_pds_ble_reset(const struct shell *sh, size_t argc,
				char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	qcc74x_pm_pds15_ble_diag_reset();
	shell_print(sh, "lp_pds ble: diagnostics reset");
	return 0;
}

static int cmd_lp_pds_ble_inject_crash(const struct shell *sh, size_t argc,
				       char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	qcc74x_pm_pds15_ble_inject_crash();
	shell_print(sh,
		    "lp_pds ble: next PDS15 wake will exercise BLE crash recovery");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_lp_pds_ble,
	SHELL_CMD_ARG(show, NULL, "Show BLE PDS15 diagnostic counters",
		      cmd_lp_pds_ble_show, 1, 0),
	SHELL_CMD_ARG(reset, NULL, "Reset BLE PDS15 diagnostic counters",
		      cmd_lp_pds_ble_reset, 1, 0),
	SHELL_CMD_ARG(inject_crash, NULL,
		      "Force next PDS15 wake into BLE crash recovery path",
		      cmd_lp_pds_ble_inject_crash, 1, 0),
	SHELL_SUBCMD_SET_END);
#endif /* CONFIG_QCC74X_PM_PDS15_BLE */

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_lp_pds_wifi,
	SHELL_CMD_ARG(show, NULL, "Show last Wi-Fi PDS15 diagnostic sample",
		      cmd_lp_pds_wifi_show, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_lp_pds,
	SHELL_CMD_ARG(show, NULL, "Show low-power LPFW init and PDS15 status",
		      cmd_lp_pds_show, 1, 0),
	SHELL_CMD_ARG(auto, NULL,
		      "Control automatic PDS15 policy gate. Usage: lp_pds auto <on [duration_ms]|off|show>",
		      cmd_lp_pds_auto, 2, 1),
	SHELL_CMD(wifi, &sub_lp_pds_wifi, "Show Wi-Fi PDS15 diagnostics",
		  NULL),
#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
	SHELL_CMD(ble, &sub_lp_pds_ble,
		  "Show/reset BLE PDS15 diagnostics or inject crash test",
		  NULL),
#endif
	SHELL_CMD_ARG(reset, NULL,
		      "Cancel any pending arm and reset low-power diagnostics",
		      cmd_lp_pds_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((qcom), pm, &sub_lp_pds, "low-power commands", NULL, 1, 0);

#endif /* CONFIG_WIFI_SHELL_LOWPOWER_VALIDATION */
