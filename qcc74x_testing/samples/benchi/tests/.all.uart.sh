# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

TRIGGER_DIR=$(realpath $(dirname $BASH_SOURCE)/uart)
cd "$TRIGGER_DIR"


for script in "$TRIGGER_DIR"/*.sh; do

    if [[ "$(basename "$script")" == ".uart.sh" ]]; then
        continue
    fi

    chmod +x "$script"
    echo "Trigger script: $script"
    "$script"
done

echo "All UART scripts triggerd!"
