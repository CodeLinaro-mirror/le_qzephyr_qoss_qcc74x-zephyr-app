
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

zephyr_sources(src/platform_shell.c)
zephyr_sources_ifdef(CONFIG_WIFI src/qwifi_shell.c)
