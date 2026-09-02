# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)

# boards/qcc748m_evk.conf and boards/qcc748m_evk.overlay are auto-detected
# by Zephyr from this project's boards/ directory — no explicit references needed.
# All shell modules (async_test_shell, i2c_eeprom_async, crypto, flash, psram...)
# are provided by qapp/qcc74x/common/shell/ via module.yml auto-inclusion.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(qcli_app)

target_sources(app PRIVATE src/main.c)


