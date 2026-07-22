# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

SSID="$1"
PASSPHRASE="$2"

WIFI_CONFIG_FILE=$TESTS_DIR/wifi.config

if [ -f $WIFI_CONFIG_FILE ]; then
    if [ "$SSID" = "" ]; then
        SSID="$(sed -n '1p' $WIFI_CONFIG_FILE)"
    fi
    if [ "$PASSPHRASE" = "" ]; then
        PASSPHRASE="$(sed -n '2p' $WIFI_CONFIG_FILE)"
    fi
else
    (
        echo "DUMMY_SSID"
        echo "DummyPassphrase"
    ) > $WIFI_CONFIG_FILE
fi

if [ "$SSID" = "" ] || [ "$PASSPHRASE" = "" ]; then
    echo "Error: You must specify Wifi SSID and Passphrase, or maintain it in '$WIFI_CONFIG_FILE'"
    exit 1
fi


# send wifi connect command
send_uart --wait "net_dhcpv4" --wait-max 30 --verbose \
    -- wifi connect -s "$SSID" -p "$PASSPHRASE" -k 1

