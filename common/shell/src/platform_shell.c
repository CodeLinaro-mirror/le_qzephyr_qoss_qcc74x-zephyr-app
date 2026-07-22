/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <zephyr/shell/shell.h>
#include <string.h>

#if CONFIG_WIFI || CONFIG_BT
#include <qcc74x_sys.h>
#endif

static int cmd_platform_reboot(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: platform reboot --por|--sw|--edl");
        return -EINVAL;
    }

#if CONFIG_WIFI || CONFIG_BT
    if (!strcmp(argv[1], "--por")) {
        shell_print(sh, "Rebooting: power-on reset (POR)...");
        return qc7xx_sys_reset_por();
    } else if (!strcmp(argv[1], "--sw")) {
        shell_print(sh, "Rebooting: software reset...");
        qc7xx_sys_reset_system();
    } else if (!strcmp(argv[1], "--edl")) {
        shell_print(sh, "Rebooting: reset to EDL (emergency download) mode...");
        qc7xx_sys_reset_system_from_interface();
    } else {
        shell_error(sh, "Unknown reboot type '%s'. Usage: platform reboot --por|--sw|--edl", argv[1]);
        return -EINVAL;
    }

    /* Unreachable: the reset paths above never return. */
    return 0;
#else
    ARG_UNUSED(argv);
    shell_error(sh, "platform reboot requires CONFIG_WIFI or CONFIG_BT");
    return -ENOTSUP;
#endif
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_platform_cmds,
                                SHELL_CMD_ARG(reboot, NULL,
                                              "Reboot the system.\n"
                                              "Usage: platform reboot --por|--sw|--edl\n"
                                              "  --por: power-on reset\n"
                                              "  --sw : software (system) reset\n"
                                              "  --edl: reset to EDL (emergency download) mode\n",
                                              cmd_platform_reboot, 2, 0),
                                SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(platform, &sub_platform_cmds, "platform commands", NULL);
