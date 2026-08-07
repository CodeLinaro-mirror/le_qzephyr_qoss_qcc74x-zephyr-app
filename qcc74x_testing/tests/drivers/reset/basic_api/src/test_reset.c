/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dt-bindings/reset/qcc7xx_reset.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/devicetree.h>

#define RESET_NODE DT_NODELABEL(reset)

#define MY_PERIPH_NODE      DT_NODELABEL(uart1)
#define MY_PERIPH_BASE      DT_REG_ADDR(MY_PERIPH_NODE)
#define MY_PERIPH_REG       (MY_PERIPH_BASE + 0x08)
#define MY_PERIPH_RESET_ID  QC7XX_RESET_UART1
#define MY_PERIPH_RESET_VAL 0x00FF00FF
#define MY_PERIPH_SET_VAL   0x00000000

uint32_t resets[] = {QC7XX_RESET_WIFI, QC7XX_RESET_UART1, QC7XX_RESET_I2C0, QC7XX_RESET_I2S};

static int test_reset_status(void)
{
	const struct device *const rst = DEVICE_DT_GET(RESET_NODE);

	if (!device_is_ready(rst)) {
		TC_PRINT("Reset device is not ready\n");
		return TC_FAIL;
	}

	TC_PRINT("Testcase: %s\n", __func__);

	for (uint32_t i = 0; i < (sizeof(resets) / sizeof(resets[0])); i++) {
		uint8_t stat = 0xFF;
		int res = reset_status(rst, resets[i], &stat);
		if (res != 0) {
			TC_PRINT("Reset status read failed id: %u\n", resets[i]);
			return TC_FAIL;
		}
		if (stat != 0) {
			TC_PRINT("Unexpected reset status id: %u\n", resets[i]);
			return TC_FAIL;
		}

		res = reset_line_assert(rst, resets[i]);
		if (res != 0) {
			TC_PRINT("Asserting reset line failed id: %u\n", resets[i]);
			return TC_FAIL;
		}

		k_msleep(100);
		res = reset_status(rst, resets[i], &stat);
		if (res != 0) {
			TC_PRINT("Reset status read failed id: %u\n", resets[i]);
			return TC_FAIL;
		}
		if (stat == 0) {
			TC_PRINT("Unexpected reset status id: %u\n", resets[i]);
			return TC_FAIL;
		}

		res = reset_line_deassert(rst, resets[i]);
		if (res != 0) {
			TC_PRINT("Deasserting reset line failed id: %u\n", resets[i]);
			return TC_FAIL;
		}

		k_msleep(100);
		res = reset_status(rst, resets[i], &stat);
		if (res != 0) {
			TC_PRINT("Reset status read failed id: %u\n", resets[i]);
			return TC_FAIL;
		}
		if (stat != 0) {
			TC_PRINT("Unexpected reset status id: %u\n", resets[i]);
			return TC_FAIL;
		}
	}

	TC_PRINT("Reset status test successful\n");
	return TC_PASS;
}

static int test_reset_peripheral(void)
{
	const struct device *const rst = DEVICE_DT_GET(RESET_NODE);

	if (!device_is_ready(rst)) {
		TC_PRINT("Reset device is not ready\n");
		return TC_FAIL;
	}

	TC_PRINT("Testcase: %s\n", __func__);

	sys_write32(MY_PERIPH_SET_VAL, MY_PERIPH_REG);
	if (sys_read32(MY_PERIPH_REG) != MY_PERIPH_SET_VAL) {
		TC_PRINT("Failed to set the register value");
		return TC_FAIL;
	}

	TC_PRINT("Resetting peripheral, reset id: %u\n", MY_PERIPH_RESET_ID);
	reset_line_toggle(rst, MY_PERIPH_RESET_ID);
	k_msleep(300);
	uint32_t val = sys_read32(MY_PERIPH_REG);
	if (val != MY_PERIPH_RESET_VAL) {
		TC_PRINT("Register value read: %08X\nRegister value expected: %08X\n",
			 val, MY_PERIPH_RESET_VAL);
		return TC_FAIL;
	}
	TC_PRINT("Peripheral reset success\n");
	return TC_PASS;
}

static int test_reset_system(void)
{
	const struct device *const rst = DEVICE_DT_GET(RESET_NODE);
	uint32_t cause = 0U;

	TC_PRINT("Testcase: %s\n", __func__);

	if (!device_is_ready(rst)) {
		TC_PRINT("Reset device is not ready\n");
		return TC_FAIL;
	}

	if (hwinfo_get_reset_cause(&cause) == 0 && (cause & RESET_POR) != 0U) {
		TC_PRINT("System reset tested ok, reset cause: 0x%08X\n", cause);
		hwinfo_clear_reset_cause();
		return TC_PASS;
	}

	hwinfo_clear_reset_cause();
	k_msleep(300);
	reset_line_toggle(rst, QC7XX_RESET_PWRON);

	TC_PRINT("Execution continued after asserting reset, system reset test failed");
	return TC_FAIL;
}

ZTEST(reset_basic_test_suite, test_reset)
{
	zassert_true(test_reset_system() == TC_PASS);
	zassert_true(test_reset_status() == TC_PASS);
	zassert_true(test_reset_peripheral() == TC_PASS);
}
