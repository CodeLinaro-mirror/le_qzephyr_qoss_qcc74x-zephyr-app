# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth_shell)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

target_sources(app PRIVATE
        src/btx_shell.c
)
