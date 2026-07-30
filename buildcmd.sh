#!/bin/bash

#========================================================================
#Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#SPDX-License-Identifier: BSD-3-Clause-Clear
#integration entry
#========================================================================
bash setenv.sh
ws1="$(pwd)"
echo $ws1
cd ../..
basedirs="$(pwd)"
if [ ! -f SRC-IOE-SDK.tar.gz ]; then
    tar --exclude=.git --exclude=.gitignore -czpf SRC-IOE-SDK.tar.gz west-manifest modules/hal/qcom/qcc74x qcc74x-zephyr \
    qapp/qcc74x prop/qcc74x-bin modules/lib/hostap zephyr modules/fs/littlefs modules/crypto/mbedtls modules/debug/segger modules/lib/zcbor modules/hal/cmsis_6 modules/fs/fatfs modules/lib/gui/lvgl modules/crypto/tf-psa-crypto 
fi
cd zephyr
git update-ref refs/heads/manifest-rev $(git rev-parse HEAD)
cd ..
west init -l west-manifest --mf west_qcc740_gerrit_dev.yml
cd qapp/qcc74x
west build -b qcc748m_evk samples/net/wifi/shell -d build/wifishell
