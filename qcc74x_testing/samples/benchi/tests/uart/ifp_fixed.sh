# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

run_test_uart $(basename $BASH_SOURCE)+ifp-$size.csv --wait-max 30 \
    bshg trigger json '{"ll":[{"file":[{"action":"raw_ifp_fixed", "offset":100, "size":"512"}]}]}'

