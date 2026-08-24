# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo_fs)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/flash_littlefs.c)
target_sources(app PRIVATE src/flash_raw.c)
