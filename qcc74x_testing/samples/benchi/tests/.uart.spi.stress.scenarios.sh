# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

SCRIPT_DIR=$(realpath $(dirname $BASH_SOURCE))

# stress "percent - timebase in us"
stress=(
    "0 0"
    "10 100"
    "20 100"
    "30 100"
    "40 100"
    "50 100"
    "60 100"
    "70 100"
    "80 100" 
    "0 0"       
    "10 200"
    "20 200"
    "30 200"
    "40 200"
    "50 200"
    "60 200"
    "70 200"   
    "80 200"   
    "0 0"         
    "10 500"
    "20 500"
    "30 500"
    "40 500"
    "50 500"
    "60 500"
    "70 500"   
    "80 500"  
    "0 0"         
    "10 1000"
    "20 1000"
    "30 1000"
    "40 1000"
    "50 1000"
    "60 1000"
    "70 1000"            
    "80 1000"           
)

source $(realpath $(dirname $BASH_SOURCE))/.init.test.sh

for s in "${stress[@]}"; do

    percent=$(echo $s | cut -d ' ' -f1)
    timebase=$(echo $s | cut -d ' ' -f2)

    set_stress_cpu "$percent" "$timebase"
    run_test_uart $(basename $BASH_SOURCE)_stress_$percent-$timebase.csv \
        bshg trigger json '{"ll":[{"spi":[{"data":"DEADBEEF0123456789ABCDEF"}]}]}'
done