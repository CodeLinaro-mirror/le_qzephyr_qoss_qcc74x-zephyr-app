# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

#ensure_wifi_connected_and_reachable

DEVICE_GATEWAY=$(send_uart --verbose net iface | grep "IPv4 gateway" | grep -oP '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
HOST_IP=$(ip route get $DEVICE_GATEWAY | head -n1 | cut -d" " -f5)
DEVICE_IP=$(getent hosts bsh-benchi | awk '{ print $1 }')

######################################
# start iperf server on device
######################################
echo "UDP RX START"
send_uart --wait-max 40 --verbose \-- iperf -s -u -i 1 -t 10 

# run iperf (connect to client)
iperf -c $DEVICE_IP -u -i 1 -t 10 -b 50M

# stop zperf server on device
send_uart --verbose \-- iperf -a
echo "UDP RX END"



######################################
# start iperf server on host
######################################
killall iperf || :

echo "UDP TX START"
# start server
iperf -s -u -i 1 &

# run zperf client on device
STDOUT=$(send_uart --verbose --wait "iperf exit" --wait-max 40 \-- iperf -c $HOST_IP -u -i 1 -t 10 | tee /dev/tty) # print live as well

######################################
# start iperf server on device
######################################
#send_uart --wait-max 40 --verbose \-- iperf -s -i 1 -t 10

# run iperf (connect to client)
#iperf -c $DEVICE_IP -i 1 -t 10

# stop zperf server on device
#send_uart --verbose \-- iperf -a

# cleanup
killall iperf || :
echo "UDP TX END"
# check for failure
#FAILED=$(echo "$STDOUT" | grep "net_zperf: Connect failed")
#if [ "$FAILED" != "" ]; then
#    echo "Error: Connection failed. Please make sure that your port 5001 is open, e.g. by running 'sudo ufw allow 5001'"
#    exit 1
#fi

