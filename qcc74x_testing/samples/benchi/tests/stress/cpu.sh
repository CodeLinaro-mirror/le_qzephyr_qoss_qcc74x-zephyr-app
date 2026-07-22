# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh


# initialize with 10 if not specified
STRESS_LEVEL_PERCENT=${1:-10}
STRESS_LEVEL_BASETIME_US=${2:-10}

send_uart bshg stress cpu $STRESS_LEVEL_PERCENT $STRESS_LEVEL_BASETIME_US
