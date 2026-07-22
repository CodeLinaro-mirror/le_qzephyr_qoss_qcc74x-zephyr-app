# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

run_test_uart $(basename $BASH_SOURCE)+1.csv \
    bshg trigger json '{"ll":[{"spi":[{"data":"aabbcc"}]}]}'


run_test_uart $(basename $BASH_SOURCE)+2.csv \
    bshg trigger json '{"ll":[{"spi":[{"data":"DEADBEEF0123456789ABCDEF","delay_ms":2}]}]}'


run_test_uart $(basename $BASH_SOURCE)+3.csv \
    bshg trigger json '{"ll":[{"spi":[{"data":"123456"}]}]}'

