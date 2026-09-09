# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_prov_wifi)

target_sources(app PRIVATE
	src/main.c
	src/qcc74xsync_ble.c
	src/qcc74xsync_ble_app.c
	src/transfer.c
	src/payload.c
	src/encrypt_layer.c
)
