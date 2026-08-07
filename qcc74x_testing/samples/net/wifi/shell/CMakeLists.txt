# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

FILE(GLOB app_sources ${ZEPHYR_BASE}/samples/net/wifi/shell/src/*.c)
target_sources(app PRIVATE ${app_sources})
target_sources_ifdef(CONFIG_WIFI_SHELL_LOWPOWER_VALIDATION app PRIVATE
  src/lp_pds_shell.c
)
target_sources_ifdef(CONFIG_WIFI_SHELL_COEX_VALIDATION app PRIVATE
  src/qcc74x_coex_shell.c
)
