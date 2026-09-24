# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usb_shell_pytest)

target_sources(app PRIVATE
    ${ZEPHYR_BASE}/samples/subsys/usb/shell/src/main.c
)
