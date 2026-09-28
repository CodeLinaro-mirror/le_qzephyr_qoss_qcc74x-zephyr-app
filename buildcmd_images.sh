#!/bin/bash

#========================================================================
#Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#SPDX-License-Identifier: BSD-3-Clause-Clear
#integration image build script
#========================================================================

west build -p always -b qcc748m_evk qcli_app -d build/qcli_app_default
west build -p always -b qcc748m_evk qcli_app -d build/qcli_app_wifi -- -DEXTRA_CONF_FILE="wifi_overlay.conf"
west build -p always -b qcc748m_evk qcli_app -d build/qcli_app_bt -- -DEXTRA_CONF_FILE="bt_overlay.conf"
west build -p always -b qcc748m_evk samples/subsys/pm/lowpower -d build/sample_lowpower
