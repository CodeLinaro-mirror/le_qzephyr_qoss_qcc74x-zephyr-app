# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

#ensure_wifi_connected_and_reachable

DEVICE_GATEWAY=$(send_uart --verbose net iface | grep "IPv4 gateway" | grep -oP '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
HOST_IP=$(ip route get $DEVICE_GATEWAY | head -n1 | cut -d" " -f5)
DEVICE_IP=$(getent hosts bsh-benchi | awk '{ print $1 }')

######################################
# start zperf server on device
######################################
send_uart --verbose \
    zperf tcp download

# run iperf (connect to client)
iperf -c $DEVICE_IP

# stop zperf server on device
send_uart --verbose \
    zperf tcp download stop




######################################
# start iperf server on host
######################################
killall iperf || :

# start server
iperf -s &

# run zperf client on device
STDOUT=$(send_uart --verbose --wait-max 30 \
    zperf tcp upload $HOST_IP 5001 \
    | tee /dev/tty # print live as well
)

# cleanup
killall iperf || :

# check for failure
FAILED=$(echo "$STDOUT" | grep "net_zperf: Connect failed")
if [ "$FAILED" != "" ]; then
    echo "Error: Connection failed. Please make sure that your port 5001 is open, e.g. by running 'sudo ufw allow 5001'"
    exit 1
fi

