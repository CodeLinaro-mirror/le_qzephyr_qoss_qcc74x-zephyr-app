# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

# raw operations
for size in 1000000 500000 100000; do
    run_test_uart $(basename $BASH_SOURCE)+raw_erase-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"raw_erase","offset":0,"size":'$size'}]}]}'

    run_test_uart $(basename $BASH_SOURCE)+raw_generate-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"raw_generate","offset":0,"size":'$size'}]}]}'

done

# filesystem operations
for size in 1000000 100000 10000 1000 100; do
    run_test_uart $(basename $BASH_SOURCE)+write-$size.csv --wait-max 30\
        bshg trigger json '{"ll":[{"file":[{"action":"generate","path":"/lfs/testfile","size":'$size'}]}]}'

    run_test_uart $(basename $BASH_SOURCE)+read-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"read","path":"/lfs/testfile"}]}]}'

    run_test_uart $(basename $BASH_SOURCE)+remove-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"remove","path":"/lfs/testfile"}]}]}'

    run_test_uart $(basename $BASH_SOURCE)+raw_generate-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"raw_clean_generate","offset":0,"size":'$size'}]}]}'

    run_test_uart $(basename $BASH_SOURCE)+raw_generate-$size.csv --wait-max 30 \
        bshg trigger json '{"ll":[{"file":[{"action":"raw_generate","offset":0,"size":'$size'}]}]}'
done


