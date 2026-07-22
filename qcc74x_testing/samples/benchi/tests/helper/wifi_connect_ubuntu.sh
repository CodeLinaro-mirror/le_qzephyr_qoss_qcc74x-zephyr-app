# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

if [[ "$EUID" -ne 0 ]]; then
  echo "Please run as root (e.g. with sudo)"
  exit 1
fi

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


CURRENT_SSID=$(sudo nmcli -t -f active,ssid dev wifi | grep '^yes' | cut -d: -f2)

# connect via nmcli
if [ "$CURRENT_SSID" = "$SSID" ]; then
(
    echo "Already connected to '$SSID'"
)
else
(
    set -x
    sudo nmcli dev wifi
    sudo nmcli device wifi connect "$SSID" password "$PASSPHRASE"
)
fi

echo "Done"
