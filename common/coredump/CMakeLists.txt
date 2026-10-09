# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_sources(qcom_coredump_runtime.c)
zephyr_sources_ifdef(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_SERIAL qcom_coredump_backend_other.c)
zephyr_sources_ifdef(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH qcom_coredump_backend_other_flash.c)
zephyr_sources_ifdef(CONFIG_QCC74X_COREDUMP_SHELL qcom_coredump_shell.c)

target_link_options(app PRIVATE -Wl,--wrap=coredump)
zephyr_ld_options(-Wl,--wrap=coredump)
