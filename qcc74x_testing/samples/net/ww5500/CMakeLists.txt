# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)

# Use sample-local DTS (zephyr,sram=&psram keeps SRAM_BASE=0xa8000000 so the
# RISC-V GP calculation keeps itcm small enough for the ROM bootloader copy).
# Without this the data_region exceeds the ROM copy limit and the board hangs.
set(DTS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/boards/qcc748m_evk.dts CACHE FILEPATH "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo_ethernet_ww5500)
target_sources(app PRIVATE src/main.c)
