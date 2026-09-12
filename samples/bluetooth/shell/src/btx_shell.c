/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include "host/shell/bt.h"
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include "common/bt_shell_private.h"
#include <zephyr/sys/byteorder.h>


static int cmd_adv_param(const struct shell *sh,
                         size_t argc,
                         char *argv[])
{
    struct net_buf *buf;
    uint8_t *cp;
    uint16_t interval_ms;
    uint16_t interval_units;
    uint8_t adv_type = 0x00; /* Default: ADV_IND */;
    int err;

    if (argc != 2 && argc != 3) {
        shell_error(sh,
            "Usage: btx adv-param <interval_ms> <ind|direct|scan|nonconn>");
        return -EINVAL;
    }

    interval_ms = strtoul(argv[1], NULL, 10);

    /* BLE interval unit = 0.625 ms */
    interval_units = (interval_ms * 1000U) / 625U;

    if (interval_units < 0x20 || interval_units > 0x4000) {
        shell_error(sh,
                    "invalid interval %u ms (0x%04x)",
                    interval_ms,
                    interval_units);
        return -EINVAL;
    }

    if (argc == 3) {
        if (!strcmp(argv[2], "ind")) {
            adv_type = 0x00;      /* ADV_IND */
        } else if (!strcmp(argv[2], "direct")) {
            adv_type = 0x01;      /* ADV_DIRECT_IND */
        } else if (!strcmp(argv[2], "scan")) {
            adv_type = 0x02;      /* ADV_SCAN_IND */
        } else if (!strcmp(argv[2], "nonconn")) {
            adv_type = 0x03;      /* ADV_NONCONN_IND */
        } else {
            shell_error(sh,
                    "invalid adv type: %s",
                    argv[2]);
            return -EINVAL;
        }
    }

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        shell_error(sh, "No HCI buffer");
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 15);

    /* Advertising_Interval_Min */
    cp[0] = interval_units & 0xff;
    cp[1] = interval_units >> 8;

    /* Advertising_Interval_Max */
    cp[2] = interval_units & 0xff;
    cp[3] = interval_units >> 8;

    /* Advertising_Type */
    cp[4] = adv_type;

    /* Own_Address_Type */
    cp[5] = 0x00;

    /* Peer_Address_Type */
    cp[6] = 0x00;

    /* Peer_Address */
    memset(&cp[7], 0, 6);

    /* Channel Map = 37+38+39 */
    cp[13] = 0x07;

    /* Filter Policy */
    cp[14] = 0x00;

    err = bt_hci_cmd_send_sync(0x2006, buf, NULL);
    if (err) {
        shell_error(sh, "HCI cmd failed (%d)", err);
        return err;
    }

    shell_print(sh,
                "Advertising interval=%u ms, type=0x%02x",
                interval_ms,
                adv_type);

    return 0;
}


static int cmd_adv(const struct shell *sh,
                   size_t argc,
                   char *argv[])
{
    struct net_buf *buf;
    uint8_t *cp;
    uint8_t enable;
    int err;

    if (argc != 2) {
        shell_print(sh, "Usage: btx adv <on|off>");
        return -EINVAL;
    }

    if (!strcmp(argv[1], "on")) {
        enable = 0x01;
    } else if (!strcmp(argv[1], "off")) {
        enable = 0x00;
    } else {
        shell_error(sh, "Invalid parameter: %s", argv[1]);
        shell_print(sh, "Usage: btx adv <on|off>");
        return -EINVAL;
    }

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        shell_error(sh, "No HCI buffer");
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 1);
    cp[0] = enable;

    /* LE Set Advertising Enable (OCF=0x000A, OGF=0x08) */
    err = bt_hci_cmd_send_sync(0x200A, buf, NULL);
    if (err) {
        shell_error(sh,
                    "%s advertising failed (%d)",
                    enable ? "Enable" : "Disable",
                    err);
        return err;
    }

    shell_print(sh,
                "Advertising %s",
                enable ? "enabled" : "disabled");

    return 0;
}

static int cmd_adv_data(const struct shell *sh,
                        size_t argc,
                        char *argv[])
{
    struct net_buf *buf;
    uint8_t *cp;
    int err;

    /*-------------------------------
     * Scan Response Data
     * argv[1] = ScanRsp Name
     *-------------------------------*/
    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 32);
    memset(cp, 0, 32);

    if (argc >= 2) {
        size_t len = strlen(argv[1]);

        if (len > 29) {
            shell_error(sh, "scan rsp name too long");
            return -EINVAL;
        }

        cp[0] = len + 1;
        cp[1] = 0x09;      /* Complete Local Name */
        memcpy(&cp[2], argv[1], len);
    }

    err = bt_hci_cmd_send_sync(BT_OP(BT_OGF_LE, 0x0009),
                               buf, NULL);
    if (err) {
        shell_error(sh,
                    "Set scan response failed (%d)",
                    err);
        return err;
    }


    /*-------------------------------
     * Advertising Data (optional)
     * argv[2] = Adv Name
     *-------------------------------*/
    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 32);
    memset(cp, 0, 32);

    if (argc >= 3) {
        size_t len = strlen(argv[2]);

        if (len > 29) {
            shell_error(sh, "adv name too long");
            return -EINVAL;
        }

        cp[0] = len + 1;
        cp[1] = 0x09;      /* Complete Local Name */
        memcpy(&cp[2], argv[2], len);
    }

    err = bt_hci_cmd_send_sync(BT_OP(BT_OGF_LE, 0x0008),
                               buf, NULL);
    if (err) {
        shell_error(sh,
                    "Set advertising data failed (%d)",
                    err);
        return err;
    }


    shell_print(sh,
                "ScanRsp=\"%s\" AdvName=\"%s\"",
                (argc >= 2) ? argv[1] : "",
                (argc >= 3) ? argv[2] : "");

    return 0;
}

static int cmd_scan_param(const struct shell *sh,
                          size_t argc,
                          char *argv[])
{
    struct net_buf *buf;
    uint8_t *cp;
    uint16_t interval_ms;
    uint16_t window_ms;
    uint16_t interval_units;
    uint16_t window_units;
    uint8_t scan_type = 0x01; /* Default: Active Scan */
    int err;

    if (argc != 3 && argc != 4) {
        shell_error(sh,
            "Usage: btx scan_param <interval_ms> <window_ms> [active|passive]");
        return -EINVAL;
    }

    interval_ms = strtoul(argv[1], NULL, 10);
    window_ms   = strtoul(argv[2], NULL, 10);

    if (argc == 4) {
        if (!strcmp(argv[3], "active")) {
            scan_type = 0x01;
        } else if (!strcmp(argv[3], "passive")) {
            scan_type = 0x00;
        } else {
            shell_error(sh,
                        "invalid scan type: %s",
                        argv[3]);
            return -EINVAL;
        }
    }

    /* BLE scan interval/window unit = 0.625 ms */
    interval_units = (interval_ms * 1000U) / 625U;
    window_units   = (window_ms * 1000U) / 625U;

    if (interval_units < 0x0004 || interval_units > 0x4000) {
        shell_error(sh, "invalid scan interval");
        return -EINVAL;
    }

    if (window_units < 0x0004 || window_units > 0x4000) {
        shell_error(sh, "invalid scan window");
        return -EINVAL;
    }

    if (window_units > interval_units) {
        shell_error(sh,
                    "scan window must <= scan interval");
        return -EINVAL;
    }

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        shell_error(sh, "No HCI buffer");
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 7);

    /* Scan Type */
    cp[0] = scan_type;

    /* Scan Interval */
    cp[1] = interval_units & 0xff;
    cp[2] = interval_units >> 8;

    /* Scan Window */
    cp[3] = window_units & 0xff;
    cp[4] = window_units >> 8;

    /* Own Address Type */
    cp[5] = 0x00;

    /* Filter Policy */
    cp[6] = 0x00;

    err = bt_hci_cmd_send_sync(BT_OP(BT_OGF_LE, 0x000B),
                               buf, NULL);
    if (err) {
        shell_error(sh,
                    "Set scan parameters failed (%d)",
                    err);
        return err;
    }

    shell_print(sh,
                "Scan: type=%s interval=%u ms window=%u ms",
                scan_type ? "active" : "passive",
                interval_ms,
                window_ms);

    return 0;
}

static int cmd_scan(const struct shell *sh,
                    size_t argc,
                    char *argv[])
{
    struct net_buf *buf;
    uint8_t *cp;
    uint8_t enable;
    int err;

    if (argc != 2) {
        shell_error(sh, "Usage: btx scan <on|off>");
        return -EINVAL;
    }

    if (!strcmp(argv[1], "on")) {
        enable = 0x01;
    } else if (!strcmp(argv[1], "off")) {
        enable = 0x00;
    } else {
        shell_error(sh, "Invalid parameter: %s", argv[1]);
        return -EINVAL;
    }

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        shell_error(sh, "No HCI buffer");
        return -ENOMEM;
    }

    cp = net_buf_add(buf, 2);

    /* Scan_Enable */
    cp[0] = enable;

    /* Filter_Duplicates = Disable */
    cp[1] = 0x00;

    err = bt_hci_cmd_send_sync(BT_OP(BT_OGF_LE, 0x000C),
                               buf,
                               NULL);
    if (err) {
        shell_error(sh,
                    "%s scan failed (%d)",
                    enable ? "Enable" : "Disable",
                    err);
        return err;
    }

    shell_print(sh,
                "Scanning %s",
                enable ? "enabled" : "disabled");

    return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(btx_cmds,


      SHELL_CMD_ARG(adv-param,
        NULL,
        "Set advertising parameters\n"
        "Usage: adv-param <interval_ms> <ind|direct|scan|nonconn>",
        cmd_adv_param,
        2,
        1),
    SHELL_CMD_ARG(adv-data,
        NULL,
        "adv-data [scan_rsp_name] [adv_name]",
        cmd_adv_data,
        1,
        2),
    SHELL_CMD_ARG(adv, NULL,
        "Advertising control: adv <on|off>",
        cmd_adv, 2, 0),
    SHELL_CMD_ARG(scan-param,
        NULL,
        "scan-param <interval_ms> <window_ms> [active|passive]",
        cmd_scan_param,
        3, /* cmd + interval + window */
        1  /* optional scan type */),
    SHELL_CMD_ARG(scan,
        NULL,
        "scan <on|off>",
        cmd_scan,
        2,
        0),

    SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((qcom), btx, &btx_cmds, "BT commands", NULL, 1, 0);
