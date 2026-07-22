# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

set -e

SCRIPTDIR=$(realpath $(dirname $BASH_SOURCE))

ZEPHYRPROJECT_DIR=$(realpath $SCRIPTDIR/../../..)

OUTPUT_CSV_FILE="$1"
if [ "$OUTPUT_CSV_FILE" = "" ]; then
    echo "Error: You have to specify your output file (csv)" && exit 1
fi

TRACEBUFFER_TMP_DIR=$ZEPHYRPROJECT_DIR/build-output/.ctf/$(date +"%Y%m%d%H%M%S%6N")
TRACING_DATA_FILE=$(realpath -m "$TRACEBUFFER_TMP_DIR/ctf/ram_tracing.bin")

(
    UART_OUTPUT=$(
        $SCRIPTDIR/send_uart.py -b 115200 --dev /dev/ttyACM1 --wait-max 10 --verbose \
            bshg get tracebuffer
    )

    LENGTH=$(echo "$UART_OUTPUT" | sed -n 's/.*--- tracebuffer start (\([0-9]*\) bytes).*/\1/p')

    LINE_START=$(echo "$UART_OUTPUT" | grep -noP '\-\-\- tracebuffer start' | cut -d: -f1)
    LINE_END=$(echo "$UART_OUTPUT" | grep -noP '\-\-\- tracebuffer end' | cut -d: -f1)

    if [ "$(($LINE_START + 2))" != "$LINE_END" ]; then
        echo "Error: It seems that something went wrong."
        exit 1
    fi

    RAM_BUFFER_LINE=$(($LINE_START))
    RAM_BUFFER_HEX=$(echo "$UART_OUTPUT" | sed -n "${RAM_BUFFER_LINE}p")
    RAM_BUFFER_HEX="${RAM_BUFFER_HEX:35:-1}" # cut the last character 0A

    # Check length: 1 byte in tracebuffer = 2 bytes in string representation (0x??)
    RAM_BUFFER_HEX_LEN="${#RAM_BUFFER_HEX}"
    if [ "$RAM_BUFFER_HEX_LEN" != "$((2*$LENGTH))" ]; then
        echo "Error: Invalid length: '${#RAM_BUFFER_HEX}' != '$LENGTH'" && exit 1
    fi
    
    # write TRACING_DATA_FILE
    mkdir -p $(dirname $TRACING_DATA_FILE)
    echo "$RAM_BUFFER_HEX" | xxd -r -p > $TRACING_DATA_FILE
)

(
    # convert to csv
    "$SCRIPTDIR/convert_ctftracebuffer.sh" "$TRACING_DATA_FILE" "$OUTPUT_CSV_FILE"
)

echo "Done!"
