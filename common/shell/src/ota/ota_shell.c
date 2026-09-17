/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ota_shell.h"

#include <errno.h>
#include <qcc74x_fota.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static atomic_t ota_running;
static const char *ota_transport;
static qcc74x_ota_shell_cancel_fn_t ota_cancel;

int qcc74x_ota_shell_session_begin(const char *transport, qcc74x_ota_shell_cancel_fn_t cancel)
{
	if (!transport || !cancel) {
		return -EINVAL;
	}

	if (!atomic_cas(&ota_running, 0, 1)) {
		return -EBUSY;
	}

	ota_transport = transport;
	ota_cancel = cancel;

	return 0;
}

void qcc74x_ota_shell_session_complete(void)
{
	ota_cancel = NULL;
	ota_transport = NULL;
	atomic_clear(&ota_running);
}

bool qcc74x_ota_shell_session_running(void)
{
	return atomic_get(&ota_running) != 0;
}

const char *qcc74x_ota_shell_session_transport(void)
{
	return ota_transport;
}

int qcc74x_ota_shell_session_cancel(void)
{
	qcc74x_ota_shell_cancel_fn_t cancel;

	if (!qcc74x_ota_shell_session_running()) {
		return -EALREADY;
	}

	/* Snapshot the cancel pointer while running is still set. */
	cancel = ota_cancel;

	if (!cancel) {
		return -ENOTSUP;
	}

	return cancel();
}

static int cmd_ota_status(const struct shell *shell, size_t argc, char **argv)
{
	char buffer[96];
	const char *transport;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qcc74x_ota_status(buffer, sizeof(buffer));
	shell_print(shell, "%s", buffer);

	transport = qcc74x_ota_shell_session_transport();
	if (transport) {
		shell_print(shell, "OTA transport: %s (running)", transport);
	}

	return 0;
}

static int cmd_ota_cancel(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = qcc74x_ota_shell_session_cancel();
	if (rc == -EALREADY) {
		shell_error(shell, "no OTA is running");
	} else if (rc) {
		shell_error(shell, "OTA cancellation failed (%d)", rc);
	} else {
		shell_print(shell, "OTA cancellation requested");
	}

	return rc;
}

static int cmd_ota_slot(const struct shell *shell, size_t argc, char **argv)
{
	char buffer[160];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = qcc74x_ota_slot(buffer, sizeof(buffer));
	if (rc) {
		shell_error(shell, "failed to read firmware slot (%d)", rc);
		return rc;
	}

	shell_print(shell, "%s", buffer);

	return 0;
}

static int cmd_ota_activate(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (qcc74x_ota_shell_session_running()) {
		shell_error(shell, "OTA is still running");
		return -EBUSY;
	}

	rc = qcc74x_ota_activate();
	shell_print(shell, "OTA activation %s (%d); reboot with qcom platform reboot --por",
		    rc ? "failed" : "written", rc);

	return rc;
}

SHELL_SUBCMD_SET_CREATE(ota_commands, (ota));

SHELL_SUBCMD_ADD((ota), status, NULL, "show OTA state", cmd_ota_status, 1, 0);
SHELL_SUBCMD_ADD((ota), cancel, NULL, "cancel the running OTA", cmd_ota_cancel, 1, 0);
SHELL_SUBCMD_ADD((ota), slot, NULL, "show active and inactive FW slots", cmd_ota_slot, 1, 0);
SHELL_SUBCMD_ADD((ota), activate, NULL, "activate verified image", cmd_ota_activate, 1, 0);

SHELL_SUBCMD_ADD((qcom), ota, &ota_commands, "QCC74x OTA commands", NULL, 1, 0);
