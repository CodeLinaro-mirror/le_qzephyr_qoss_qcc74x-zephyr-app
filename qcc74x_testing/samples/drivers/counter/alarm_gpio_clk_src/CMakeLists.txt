# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(alarm_gpio_clk_src)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

if(CONFIG_BUILD_WITH_TFM)
  target_include_directories(app PRIVATE
    $<TARGET_PROPERTY:tfm,TFM_BINARY_DIR>/api_ns/interface/include
  )
endif()
