/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>

#include "wifi_mgmr_ext.h"

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

SHELL_STATIC_SUBCMD_SET_CREATE(qcc74x_coex_cmds,
	SHELL_CMD_ARG(enable, NULL, NULL, cmd_coex_enable, 1, 0),
	SHELL_CMD_ARG(disable, NULL, NULL, cmd_coex_disable, 1, 0),
	SHELL_CMD_ARG(status, NULL, NULL, cmd_coex_status, 1, 0),
	SHELL_CMD_ARG(duty, NULL, NULL, cmd_coex_duty, 1, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(qcc74x_coex, &qcc74x_coex_cmds, NULL, NULL);
