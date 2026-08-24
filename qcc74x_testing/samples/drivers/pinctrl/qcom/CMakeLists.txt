# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pinctrl_qcom)

target_sources(app PRIVATE src/main.c)

# Dummy driver for vnd,pinctrl-device — generates PINCTRL_DT_INST_DEFINE symbols
target_sources(app PRIVATE
  ${ZEPHYR_BASE}/tests/drivers/pinctrl/common/test_device.c
)
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/tests/drivers/pinctrl/common
)
