/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define LM75_ADDR     0x48
#define LM75_TEMP_REG 0x00

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static float read_lm75_temperature(void)
{
	uint8_t buffer[2];
	int ret;

	if (!device_is_ready(i2c_dev)) {
		printf("I2C Is Not Ready.\n");
		return -1;
	}

	ret = i2c_burst_read(i2c_dev, LM75_ADDR, LM75_TEMP_REG, buffer, sizeof(buffer));
	if (ret) {
		printf("I2C Read Err: %d\n", ret);
		return -1;
	}

	int16_t temp = (buffer[0] << 8) | buffer[1];
	return (temp >> 5) * 0.125f;
}

int main(void)
{
	printf("Read Temperature From LM75 By I2C\n");

	while (1) {
		float temp = read_lm75_temperature();
		printf("Now Temp: %.2f degrees Celsius\n", (double)temp);
		k_sleep(K_MSEC(2000));
	}

	return 0;
}