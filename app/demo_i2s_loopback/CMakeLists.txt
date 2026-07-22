# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo_i2s_loopback)

target_sources(app PRIVATE src/main.c)
