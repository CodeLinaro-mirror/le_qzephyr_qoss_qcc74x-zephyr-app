# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(qcc74x_ota_fs)

target_sources(app PRIVATE
  src/test_ota_fs.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../common/fota/ota/ota.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../common/fota/fs/file_ota.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../common/fota/include
)
