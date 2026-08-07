# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Use sample-local DTS (pre-9cd0e8c, zephyr,sram=&psram for large CONFIG_SRAM_SIZE)
set(DTS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/boards/qcc748m_evk.dts CACHE FILEPATH "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo_ethernet_ww5500)
target_sources(app PRIVATE src/main.c)
