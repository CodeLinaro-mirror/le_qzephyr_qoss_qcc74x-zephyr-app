# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()
zephyr_library_sources(ota/ota.c)
zephyr_library_sources_ifdef(CONFIG_QCC74X_TCP_OTA tcp/tcp_ota.c)
zephyr_library_sources_ifdef(CONFIG_FILE_SYSTEM fs/file_ota.c)
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
