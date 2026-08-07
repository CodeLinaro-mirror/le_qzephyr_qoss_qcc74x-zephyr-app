/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "device_subsys.h"
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>

/*
 * Peripherals selected for clock control API test on QCC74x boards.
 *
 * Criteria for selection:
 * - Not used by active system drivers in the minimal test configuration
 * - Supported by qc7xx_peripheral_clock_status_get_by_id() in HAL
 * - Safe to enable/disable without impacting system stability
 *
 * Excluded:
 * - QC7XX_PERIPHERAL_UART0: used as zephyr,console (uart0)
 * - QC7XX_PERIPHERAL_CPU: system CPU clock, never safe to toggle
 * - QC7XX_PERIPHERAL_SF_CTRL: flash controller, needed for code execution
 * - QC7XX_PERIPHERAL_DMA0: may be used by active drivers
 */
static const struct device_subsys_data subsys_data[] = {
	/* I2C1: second I2C bus, not used as console or system resource */
	{.subsys = (void *)QC7XX_PERIPHERAL_I2C1, .startup_us = 0},
	/* PWM0: PWM controller, safe to toggle in minimal config */
	{.subsys = (void *)QC7XX_PERIPHERAL_PWM0, .startup_us = 0},
	/* IR: IR controller, safe to toggle */
	{.subsys = (void *)QC7XX_PERIPHERAL_IR, .startup_us = 0},
};

static const struct device_data devices[] = {
	{
		.dev = DEVICE_DT_GET_ONE(qcom_qcc74x_clock_controller),
		.subsys_data = subsys_data,
		.subsys_cnt = ARRAY_SIZE(subsys_data),
	}
};
