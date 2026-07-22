# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

    run_test_uart $(basename $BASH_SOURCE)+ifp_wifi.csv --wait-max 30\
        bshg trigger json_forever '{"ll":[{"download":[{"url":"'https://postman-echo.com/bytes/100/kb'","dst":"flashifp"}]}]}'   
