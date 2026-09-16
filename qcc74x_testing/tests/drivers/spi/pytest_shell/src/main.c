/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * SPI Shell Test Application
 * Provides shell commands for dual-board SPI master/slave testing.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_shell_test, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("qcom spi Shell Test Application started");
	LOG_INF("Use 'qcom spi' commands to configure and test SPI");
	LOG_INF("Both Master and Slave roles are supported at runtime");
	return 0;
}
