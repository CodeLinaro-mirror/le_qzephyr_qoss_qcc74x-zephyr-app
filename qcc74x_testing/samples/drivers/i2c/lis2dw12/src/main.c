/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

//#define LIS2DW12_I2C_ADDRESS    0x18   // I2C address (SA0 low)
#define LIS2DW12_I2C_ADDRESS    0x19   // I2C address (SA0 high)
#define LIS2DW12_WHO_AM_I_REG   0x0F   // WHO_AM_I register
#define LIS2DW12_WHO_AM_I_VALUE 0x44   // Expected device ID
#define LIS2DW12_CTRL1_REG      0x20   // Control register 1
#define LIS2DW12_CTRL6_REG      0x25   // Control register 6
#define LIS2DW12_OUT_X_L_REG    0x28   // X-axis low byte
#define LIS2DW12_SENSITIVITY_2G 0.061f // mg/LSB for ±2g, high-performance

#ifdef CONFIG_I2C_CALLBACK
static K_SEM_DEFINE(i2c_cb_sem, 0, 1);
static int i2c_cb_result;

static void i2c_transfer_complete(const struct device *dev, int result, void *userdata)
{
	i2c_cb_result = result;
	k_sem_give(&i2c_cb_sem);
}
#endif /* CONFIG_I2C_CALLBACK */

/* Write a single register to LIS2DW12 */
static int lis2dw12_write_reg(const struct device *i2c, uint8_t reg, uint8_t value)
{
	uint8_t tx_buf[2] = {reg, value};
	int ret;

#ifdef CONFIG_I2C_CALLBACK
	struct i2c_msg msg;

	msg.buf = tx_buf;
	msg.len = 2;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	k_sem_reset(&i2c_cb_sem);
	ret = i2c_transfer_cb(i2c, &msg, 1, LIS2DW12_I2C_ADDRESS,
			      i2c_transfer_complete, NULL);
	if (ret) {
		printk("I2C write failed: %d\n", ret);
		return ret;
	}
	k_sem_take(&i2c_cb_sem, K_FOREVER);
	return i2c_cb_result;
#else
	ret = i2c_write(i2c, tx_buf, 2, LIS2DW12_I2C_ADDRESS);
	if (ret) {
		printk("I2C write failed: %d\n", ret);
		return ret;
	}
	return 0;
#endif
}

/* Read a single register from LIS2DW12 */
static int lis2dw12_read_reg(const struct device *i2c, uint8_t reg, uint8_t *data)
{
	int ret;

#ifdef CONFIG_I2C_CALLBACK
	struct i2c_msg msgs[2];

	k_sem_reset(&i2c_cb_sem);
	ret = i2c_write_read_cb(i2c, msgs, 2, LIS2DW12_I2C_ADDRESS,
				&reg, 1, data, 1,
				i2c_transfer_complete, NULL);
	if (ret) {
		printk("I2C read failed: %d\n", ret);
		return ret;
	}
	k_sem_take(&i2c_cb_sem, K_FOREVER);
	return i2c_cb_result;
#else
	ret = i2c_write_read(i2c, LIS2DW12_I2C_ADDRESS, &reg, 1, data, 1);
	if (ret) {
		printk("I2C read failed: %d\n", ret);
		return ret;
	}
	return 0;
#endif
}

/* Read 6 bytes (X, Y, Z) from LIS2DW12 starting at OUT_X_L */
static int lis2dw12_read_axes(const struct device *i2c, int16_t *x, int16_t *y, int16_t *z)
{
	uint8_t data[6];
	// Auto-increment for multi-byte read
	uint8_t reg = LIS2DW12_OUT_X_L_REG | 0x80;
	int ret;

#ifdef CONFIG_I2C_CALLBACK
	struct i2c_msg msgs[2];

	k_sem_reset(&i2c_cb_sem);
	ret = i2c_write_read_cb(i2c, msgs, 2, LIS2DW12_I2C_ADDRESS,
				&reg, 1, data, 6,
				i2c_transfer_complete, NULL);
	if (ret) {
		printk("I2C read axes failed: %d\n", ret);
		return ret;
	}
	k_sem_take(&i2c_cb_sem, K_FOREVER);
	if (i2c_cb_result) {
		return i2c_cb_result;
	}
#else
	ret = i2c_write_read(i2c, LIS2DW12_I2C_ADDRESS, &reg, 1, data, 6);
	if (ret) {
		printk("I2C read axes failed: %d\n", ret);
		return ret;
	}
#endif

	*x = (int16_t)((data[1] << 8) | data[0]);
	*y = (int16_t)((data[3] << 8) | data[2]);
	*z = (int16_t)((data[5] << 8) | data[4]);
	return 0;
}

/* Initialize LIS2DW12: 100 Hz, ±2g, high-performance mode */
static int lis2dw12_init(const struct device *i2c)
{
	uint8_t device_id;
	int ret;

	/* Verify device ID */
	ret = lis2dw12_read_reg(i2c, LIS2DW12_WHO_AM_I_REG, &device_id);
	if (ret || device_id != LIS2DW12_WHO_AM_I_VALUE) {
		printk("Invalid device ID: 0x%02X, expected 0x%02X\n", device_id,
		       LIS2DW12_WHO_AM_I_VALUE);
		return -EIO;
	}

	printk("LIS2DW12 detected on I2C bus, WHO_AM_I: 0x%02X\n", device_id);

	/* Set CTRL1: ODR = 100 Hz (0b0101), high-performance mode (0b0100) */
	ret = lis2dw12_write_reg(i2c, LIS2DW12_CTRL1_REG, 0x54);
	if (ret) {
		printk("Failed to configure CTRL1\n");
		return ret;
	}

	/* Set CTRL6: ±2g full-scale */
	// 0x00: FS=±2g
	ret = lis2dw12_write_reg(i2c, LIS2DW12_CTRL6_REG, 0x00);
	if (ret) {
		printk("Failed to configure CTRL6\n");
		return ret;
	}

	return 0;
}

int main(void)
{
	const struct device *i2c_dev;
	int16_t x_raw, y_raw, z_raw;
	float x_g, y_g, z_g;
	int ret;

	k_msleep(1000);

	/* Get I2C device */
	i2c_dev = DEVICE_DT_GET(DT_CHOSEN(my_i2c_dev));
	if (!i2c_dev || !device_is_ready(i2c_dev)) {
		printk("I2C device is not ready\n");
		return 0;
	}

	/* Initialize LIS2DW12 */
	ret = lis2dw12_init(i2c_dev);
	if (ret) {
		printk("LIS2DW12 initialization failed\n");
		return 0;
	}

	printk("LIS2DW12 initialized successfully !\n");

	/* Main loop: Read and print accelerometer data */
	while (1) {
		ret = lis2dw12_read_axes(i2c_dev, &x_raw, &y_raw, &z_raw);
		if (ret) {
			printk("Failed to read accelerometer data\n");
		} else {
			/* Convert raw data to g (sensitivity: 0.061 mg/LSB for ±2g) */
			x_g = (x_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;
			y_g = (y_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;
			z_g = (z_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;

			printk("Accel (g): X=%.3f, Y=%.3f, Z=%.3f\n", (double)x_g, (double)y_g,
			       (double)z_g);
		}

		k_msleep(1000);
	}
	return 0;
}
