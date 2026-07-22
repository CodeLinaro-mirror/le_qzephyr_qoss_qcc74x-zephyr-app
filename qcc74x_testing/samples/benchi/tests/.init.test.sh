# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

set -e

#####################################################
# VARIABLES
#####################################################

TESTS_DIR=$(realpath $(dirname $BASH_SOURCE))
BENCHI_DIR=$(realpath $TESTS_DIR/..)
ZEPHYRPROJECT_DIR=$(realpath $TESTS_DIR/../../..)

SEND_UART=$(realpath $BENCHI_DIR/scripts/send_uart.py)
SEND_WEBSOCKET=$(realpath $BENCHI_DIR/scripts/send_websocket.py)
FETCH_TRACEBUFFER_JLINK=$(realpath $BENCHI_DIR/scripts/fetch_ctftracebuffer_via_jlink.sh)
FETCH_TRACEBUFFER_UART=$(realpath $BENCHI_DIR/scripts/fetch_ctftracebuffer_via_uart.sh)
CTF2CSV=$(realpath $BENCHI_DIR/scripts/ctf2csv.py)
STRESS_CPU=$(realpath $TESTS_DIR/stress/cpu.sh)
WIFI_CONNECT=$(realpath $TESTS_DIR/helper/wifi_connect.sh)

#####################################################
# FALLBACK VARIABLES
#####################################################

BUILD_OUTPUT_LATEST_DIR=$(realpath $ZEPHYRPROJECT_DIR/build)
RESULTS_DIR_FALLBACK=$(realpath $TESTS_DIR/results)

#####################################################
# CONFIGURABLE VARIABLES
#####################################################

BUILD_DIR=${BUILD_DIR:-$BUILD_OUTPUT_LATEST_DIR}
RESULTS_DIR="${RESULTS_DIR:-$RESULTS_DIR_FALLBACK}"

#####################################################
# HELPER
#####################################################
prepend_timestamp()
(
    TIMESTAMP=$(date +"%Y%m%d%H%M%S%6N")
    echo "$TIMESTAMP-$1"
)

#####################################################
# WIFI
#####################################################

# Check if device is connected to wifi.
is_wifi_connected()
(
    RETCODE=0
    (
        send_uart --wait "State: COMPLETED" --wait-max 0.5 \
            wifi status > /dev/null 2>&1
    ) || RETCODE="$?"
    exit $RETCODE
)

# Check if device is pingable via wifi.
is_wifi_reachable()
(
    RETCODE=0

    # first step: nslookup for DNS resolution
    nslookup bsh-benchi > /dev/null 2>&1 || RETCODE=$?

    # second approach: ping the device
    if [ "$RETCODE" = "0" ]; then
        timeout 2 ping -c 1 bsh-benchi > /dev/null 2>&1 || RETCODE=$?
    fi

    exit $RETCODE
)

# trigger device to connect with wifi network
trigger_wifi_connection()
(
    if ! is_wifi_connected; then
        $WIFI_CONNECT # Note: script takes credentials from wifi.config
    fi
)

# Ensure device is connected to wifi. Note: Might not be pingable if not in same network.
ensure_wifi_connected()
(
    trigger_wifi_connection
    if ! is_wifi_connected; then
        echo "Error: Something went wrong. Device is still not connected to wifi."
        exit 1
    fi
)

# ensure that device is connected with wifi network and reachable via wifi afterwards
ensure_wifi_connected_and_reachable()
(
    ensure_wifi_connected
    if ! is_wifi_reachable; then
        echo "Error: Device is not reachable/pingable."
        exit 1
    fi
)

#####################################################
# TRACEBUFFER
#####################################################
reset_tracebuffer()
{
    send_uart bshg trigger tracing_init
}

fetch_tracebuffer_via_jlink()
{
    echo "Info: Receiving ram_tracing buffer via jlink..."
    OUTPUT_CSV_FILE="$RESULTS_DIR/$(prepend_timestamp "$1")"
    $FETCH_TRACEBUFFER_JLINK $BUILD_DIR $OUTPUT_CSV_FILE > /dev/null
    echo "Info: Done. Results see: $OUTPUT_CSV_FILE"
    cat $OUTPUT_CSV_FILE
}

fetch_tracebuffer_via_uart()
{
    echo "Info: Receiving ram_tracing buffer via uart ..."
    OUTPUT_CSV_FILE="$RESULTS_DIR/$(prepend_timestamp "$1")"
    $FETCH_TRACEBUFFER_UART $OUTPUT_CSV_FILE > /dev/null
    echo "Info: Done. Results see: $OUTPUT_CSV_FILE"
    cat $OUTPUT_CSV_FILE
}

#####################################################
# STRESS
#####################################################

set_stress_cpu()
{
    $STRESS_CPU "$@"
}

reset_stress_cpu()
{
    $STRESS_CPU 0 0
}


#####################################################
# UART SPECIFIC
#####################################################
send_uart()
(
    pkill -f "/dev/ttyACM1" || :
    echo "-------------------------------------------"
    "$SEND_UART" -b 115200 --dev /dev/ttyACM1 "$@"
)

run_test_uart()
(
    OUTPUT_CSV_FILE="$1" && shift
    reset_tracebuffer
    send_uart "$@"
    fetch_tracebuffer_via_jlink "$OUTPUT_CSV_FILE"
)

#####################################################
# WEBSOCKET SPECIFIC
#####################################################
send_websocket()
{
    echo "-------------------------------------------"
    http_proxy= https_proxy=  $SEND_WEBSOCKET --host bsh-benchi --cert $(realpath $TESTS_DIR/../src/webserver/certs/ca_cert.pem) "$@"
}

send_websocket_wss()
{
    send_websocket --wss "$@"
}

send_websocket_ram_tracing_init()
{
    send_websocket ram_tracing_init ''
}

send_websocket_trigger_with_traceinit()
{
    send_websocket trigger_with_traceinit "$@"
}

send_websocket_trigger()
{
    send_websocket trigger "$@"
}

run_test_websocket()
(
    reset_tracebuffer

    OUTPUT_CSV_FILE_DEVICE="$1" && shift
    OUTPUT_CLIENT_CTF="$RESULTS_DIR/$(prepend_timestamp "${OUTPUT_CSV_FILE_DEVICE%.csv}.client.ctf")"
#    send_websocket_trigger_with_traceinit "$@" --ctf "$OUTPUT_CLIENT_CTF"
    send_websocket_trigger "$@" --ctf "$OUTPUT_CLIENT_CTF"

    OUTPUT_CLIENT_CSV="${OUTPUT_CLIENT_CTF%.ctf}.csv"
    $CTF2CSV -i $OUTPUT_CLIENT_CTF -o $OUTPUT_CLIENT_CSV

    fetch_tracebuffer_via_jlink "$OUTPUT_CSV_FILE_DEVICE"
)
