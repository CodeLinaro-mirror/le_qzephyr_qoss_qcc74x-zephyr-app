/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/shell/shell.h>
#include <string.h>

static int cmd_qwifi_set_tx_power(const struct shell *sh, size_t argc, char **argv)
{
    int err = 0;
    long power_dbm;
    const char *policy;

    if (argc != 3) {
        shell_error(sh, "Usage: qwifi set_tx_power <power_dBm> <safety|freerun>");
        return -EINVAL;
    }

    power_dbm = shell_strtol(argv[1], 10, &err);
    if (err) {
        shell_error(sh, "Unable to parse <power_dBm> (err %d)", err);
        return err;
    }

    policy = argv[2];
    if (strcmp(policy, "safety") != 0 && strcmp(policy, "freerun") != 0) {
        shell_error(sh, "Unknown policy '%s'. Supported: safety|freerun", policy);
        return -EINVAL;
    }

    /* TODO: dummy implementation - not yet wired to the real tx power control. */
    shell_print(sh, "Set tx power to %ld dBm, policy=%s (dummy, not applied)", power_dbm, policy);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qwifi_commands,
                               SHELL_CMD_ARG(set_tx_power, NULL,
                                             "Set the transmit power.\n"
                                             "Usage: qwifi set_tx_power <power_dBm> <safety|freerun>\n"
                                             "<power_dBm>: exact tx power, unit dBm.\n"
                                             "<policy>: safety or freerun.\n",
                                             cmd_qwifi_set_tx_power, 3, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(qwifi, &sub_qwifi_commands, "qwifi commands", NULL);
