# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sample_ir_nec)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE ${ZEPHYR_HAL_QCOM_MODULE_DIR}/drivers/lhal/src/qc7xx_ir.c)
