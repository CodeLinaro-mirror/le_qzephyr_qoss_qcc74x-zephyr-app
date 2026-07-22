# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

set -e

SCRIPTDIR=$(realpath $(dirname $BASH_SOURCE))

ZEPHYRPROJECT_DIR=$(realpath $SCRIPTDIR/../../..)

TRACING_DATA_BIN="$(realpath -m $1)"
if [ "$TRACING_DATA_BIN" = "" ]; then
    echo "Error: You have to specify ctf tracing data file" && exit 1
fi
if [ ! -f "$TRACING_DATA_BIN" ]; then
    echo "Error: Specified file '$TRACING_DATA_BIN' does not exist" && exit 1
fi

OUTPUT_CSV_FILE="$(realpath -m $2)"
if [ "$OUTPUT_CSV_FILE" = "" ]; then
    echo "Error: You have to specify your output file (csv)" && exit 1
fi

TRACEBUFFER_TMP_DIR=$(dirname $TRACING_DATA_BIN)

# fetch databuffer and reset wr and rd indices
(
    cd $TRACEBUFFER_TMP_DIR

    # create a ctf folder
    rm -rf ctf
    mkdir -p ctf

    # create symlink to metadata file from zephyr
    ln -s $(realpath $ZEPHYRPROJECT_DIR/../zephyr/subsys/tracing/ctf/tsdl/metadata) ctf/metadata

    # create symlink to TRACING_DATA_BIN
    ln -s $TRACING_DATA_BIN ctf/tracebuffer.bin

    # run babeltrace2 and create the .ctf file. Ignore errors during babeltrace
    # format:
    # - 4 bytes timestamp (delta)
    # - 1 byte event type (e.g. 0x62 for named_event)
    # - 20 bytes text
    # - 4 bytes arg0
    # - 4 bytes arg1

    # run babeltrace. Error may occur because of the trailing zeros.
    # Note: It would be too hard to estimate the correct length, so just print the error if the resulting file is empty.
    RETCODE=0
    babeltrace_error=$(babeltrace ctf 2>&1 1> $TRACING_DATA_BIN.ctf) || RETCODE=$? # may fail

    # Check if the generated CTF file is not empty. If not, then print the actual error and exit
    if [ "$RETCODE" != "0" ] && [ ! -s $TRACING_DATA_BIN.ctf ]; then
        echo "$babeltrace_error" >&2
        exit $RETCODE
    fi


    # filter for named_event
    cat $TRACING_DATA_BIN.ctf | grep named_event > $TRACING_DATA_BIN.filtered.ctf || : # may fail

    # convert trace.ctf to csv
    python3 $SCRIPTDIR/ctf2csv.py -i $TRACING_DATA_BIN.filtered.ctf -o $TRACING_DATA_BIN.csv

    mkdir -p $(dirname $OUTPUT_CSV_FILE)

    cp -f $TRACING_DATA_BIN.csv $OUTPUT_CSV_FILE

)

echo "Info: Created '$OUTPUT_CSV_FILE':"
printf '%*s\n' "$(tput cols)" '' | tr ' ' '-'
cat $OUTPUT_CSV_FILE
printf '%*s\n' "$(tput cols)" '' | tr ' ' '-'

