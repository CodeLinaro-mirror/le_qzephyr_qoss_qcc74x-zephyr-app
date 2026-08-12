/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <string.h>

#if CONFIG_WIFI || CONFIG_BT
#include <qcc74x_sys.h>
#endif

#if defined(CONFIG_SOC_SERIES_QCC74X)
#include "qc7xx_efuse.h"
#include "qc7xx_flash.h"
#include "qcc74x_sec_dbg.h"
#include "wireless_api.h"
#include "lhal_version.h"
#endif

static const char *str_or_unknown(const char *s)
{
    return (s && *s) ? s : "unknown";
}

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

static int cmd_platform_version(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "CRM Number: %s", CONFIG_QCC7XX_CRM_NUMBER);

#if defined(CONFIG_SOC_SERIES_QCC74X)
    qc7xx_efuse_device_info_type info = { 0 };
    uint8_t mac[8] = { 0 };
    uint8_t serial[8] = { 0 };
    uint32_t jedec = qc7xx_flash_get_jedec_id();
    uint32_t flash_size = qc7xx_flash_get_size();

    qc7xx_efuse_get_device_info(&info);
    (void)qc7xx_efuse_get_chipid(mac);
    Sec_Dbg_Read_Chip_ID(serial);

    shell_print(sh, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    shell_print(sh, "Module Part Number: ");
    shell_print(sh, "Manufacturing year: 0, week: 0");
    shell_print(sh, "BOM configuration: 0");
    shell_print(sh, "Chip Serial: %02x%02x%02x%02x%02x%02x%02x%02x",
                serial[0], serial[1], serial[2], serial[3],
                serial[4], serial[5], serial[6], serial[7]);
    shell_print(sh, "Board: %s", CONFIG_BOARD);
    shell_print(sh, "SoC: %s (rev 0x%02x)", CONFIG_SOC, info.version);
    shell_print(sh, "Package: %s", str_or_unknown(info.package_name));
    shell_print(sh, "Flash: %u MB (jedec %06x, %s)",
                (unsigned int)(flash_size / (1024U * 1024U)),
                (unsigned int)(jedec & 0xFFFFFFU),
                str_or_unknown(info.flash_info_name));
#ifdef CONFIG_PSRAM
    if (CONFIG_PSRAM_LENGTH > 0) {
        shell_print(sh, "PSRAM: %u MB (%s)",
                    (unsigned int)(CONFIG_PSRAM_LENGTH / (1024U * 1024U)),
                    str_or_unknown(info.psram_info_name));
    } else {
        shell_print(sh, "PSRAM: %s (size not configured)",
                    str_or_unknown(info.psram_info_name));
    }
#else
    shell_print(sh, "PSRAM: disabled");
#endif
    shell_print(sh, "Wi-Fi Host Rev: %s", WIRELESS_VERSION);
    shell_print(sh, "LHAL Version: %s", LHAL_VERSION);
#else /* !CONFIG_SOC_SERIES_QCC74X */
    shell_print(sh, "MAC address: unknown");
    shell_print(sh, "Module Part Number: ");
    shell_print(sh, "Manufacturing year: 0, week: 0");
    shell_print(sh, "BOM configuration: 0");
    shell_print(sh, "Chip Serial: unknown");
    shell_print(sh, "Board: %s", CONFIG_BOARD);
    shell_print(sh, "SoC: %s", CONFIG_SOC);
    shell_print(sh, "Package: unknown");
    shell_print(sh, "Flash: unknown");
    shell_print(sh, "PSRAM: unknown");
#endif
    shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
    shell_print(sh, "Build: " __DATE__ " " __TIME__);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_platform_cmds,
                                SHELL_CMD_ARG(reboot, NULL,
                                              "Reboot the system.\n"
                                              "Usage: platform reboot --por|--sw|--edl\n"
                                              "  --por: power-on reset\n"
                                              "  --sw : software (system) reset\n"
                                              "  --edl: reset to EDL (emergency download) mode\n",
                                              cmd_platform_reboot, 2, 0),
                                SHELL_CMD_ARG(version, NULL,
                                              "Print platform / firmware version info",
                                              cmd_platform_version, 1, 0),
                                SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(platform, &sub_platform_cmds, "platform commands", NULL);
