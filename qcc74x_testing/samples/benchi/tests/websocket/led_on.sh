# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

#ensure_wifi_connected_and_reachable

# send websocket message to device
run_test_websocket "$(basename $BASH_SOURCE).csv" --loop 12 \
    '{"ll":[{"led":"on"}]}'

run_test_websocket "$(basename $BASH_SOURCE)-wss.csv" --wss --loop 12 \
    '{"ll":[{"led":"on"}]}'

