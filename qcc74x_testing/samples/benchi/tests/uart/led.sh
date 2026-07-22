# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

# let the board blink by sending messages

for i in $(seq 1 3); do
    run_test_uart $(basename $BASH_SOURCE)+$i.1.csv \
        bshg trigger json '{"ll":[{"led":"on"}]}'

    run_test_uart $(basename $BASH_SOURCE)+$i.2.csv \
        bshg trigger json '{"ll":[{"led":"off"}]}'
        
done

