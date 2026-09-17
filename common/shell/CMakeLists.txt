# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

zephyr_sources(src/platform_shell.c)
zephyr_sources_ifdef(CONFIG_QCOM_SPI_SHELL      src/spi_shell_qcom.c)
zephyr_sources_ifdef(CONFIG_WIFI                  src/qwifi_shell.c)
zephyr_sources_ifdef(CONFIG_QCOM_SHELL_ASYNC      src/shell_async.c)
zephyr_sources_ifdef(CONFIG_QCOM_SHELL_ASYNC      src/shell_async_test.c)
zephyr_sources_ifdef(CONFIG_QCOM_I2C_SHELL        src/i2c_shell.c)
zephyr_sources_ifdef(CONFIG_QCOM_ACOMP_SHELL      src/acomp_shell_qcom.c)
zephyr_sources_ifdef(CONFIG_PSA_CRYPTO            src/psa_shell_qcom.c)
zephyr_sources_ifdef(CONFIG_QCC74X_TCP_OTA_SHELL  src/ota/ota_shell.c)
zephyr_sources_ifdef(CONFIG_QCC74X_TCP_OTA_SHELL  src/ota/tcp_ota_shell.c)
