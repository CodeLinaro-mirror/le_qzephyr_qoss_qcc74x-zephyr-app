# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

set -e

SCRIPTDIR=$(realpath $(dirname $BASH_SOURCE))

ZEPHYRPROJECT_DIR=$(realpath $SCRIPTDIR/../../..)

JLINK_DIR=$(realpath $ZEPHYRPROJECT_DIR/.jlink/JLink_Linux_V812d_x86_64)
TRACING_DIR=$(realpath $ZEPHYRPROJECT_DIR/.tracing/SystemView_Linux_V360_x86_64)

BUILD_OUTPUT_DIR="$1"
if [ "$BUILD_OUTPUT_DIR" = "" ]; then
    echo "Error: You have to specify your build output directory" && exit 1
fi

OUTPUT_CSV_FILE="$2"
if [ "$OUTPUT_CSV_FILE" = "" ]; then
    echo "Error: You have to specify your output file (csv)" && exit 1
fi

TRACEBUFFER_TMP_DIR=$ZEPHYRPROJECT_DIR/build-output/.ctf/$(date +"%Y%m%d%H%M%S%6N")
TRACEBUFFER_BIN=$(realpath -m "$TRACEBUFFER_TMP_DIR/tracebuffer.bin")

# fetch databuffer and reset wr and rd indices
(
#    DEVICE=$(cat $BUILD_OUTPUT_DIR/zephyr/runners.yaml | grep -oP "\-\-device=.*" | grep -v : | head -n1 | cut -d= -f2)
    DEVICE=RV64
    CONFIG_RAM_TRACING_BUFFER_SIZE=$(cat $BUILD_OUTPUT_DIR/zephyr/.config | grep CONFIG_RAM_TRACING_BUFFER_SIZE | cut -d= -f2)
#    CONFIG_RAM_TRACING_BUFFER_SIZE=131072
    CONFIG_RAM_TRACING_BUFFER_SIZE=$(printf '%x' "$CONFIG_RAM_TRACING_BUFFER_SIZE") # in hex
    ram_tracing=$(cat $BUILD_OUTPUT_DIR/zephyr/zephyr.map | grep -E "0x[0-9a-f]+\s+ram_tracing\$" | grep -oP "0x[0-9a-f]+")
#    ram_tracing=0x00000000a86d11b8 

    mkdir -p $(dirname $TRACEBUFFER_BIN)

    cd $TRACEBUFFER_TMP_DIR

    # create config.jilink
    JLINK_SCRIPT="config.jlink"
    cat $SCRIPTDIR/template.jlink > $JLINK_SCRIPT

    # replace the template values
    sed -i "s#@@DEVICE@@#$DEVICE#g" $JLINK_SCRIPT
    sed -i "s#@@CONFIG_RAM_TRACING_BUFFER_SIZE@@#$CONFIG_RAM_TRACING_BUFFER_SIZE#g" $JLINK_SCRIPT
    sed -i "s#@@ram_tracing@@#$ram_tracing#g" $JLINK_SCRIPT
    sed -i "s#@@TRACEBUFFER_BIN@@#$TRACEBUFFER_BIN#g" $JLINK_SCRIPT

    # run JLinkExe
#    $JLINK_DIR/JLinkExe -CommanderScript "$JLINK_SCRIPT" > /dev/null
    $JLINK_DIR/JLinkExe -jtagconf -1,-1 -CommanderScript "$JLINK_SCRIPT" > /dev/null
)

(
    # convert to csv
    "$SCRIPTDIR/convert_ctftracebuffer.sh" "$TRACEBUFFER_BIN" "$OUTPUT_CSV_FILE"
)

echo "Done!"
