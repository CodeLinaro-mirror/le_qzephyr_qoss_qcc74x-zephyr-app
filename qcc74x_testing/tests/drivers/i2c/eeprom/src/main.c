/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define EEPROM_NODE DT_CHOSEN(zephyr_eeprom)

#if !DT_NODE_HAS_STATUS_OKAY(EEPROM_NODE)
#error "zephyr,eeprom chosen node is missing or disabled"
#endif

#define EEPROM_PAGE_SIZE DT_PROP(EEPROM_NODE, pagesize)

static const struct device *const eeprom = DEVICE_DT_GET(EEPROM_NODE);

static uint8_t __aligned(32) write_buf[EEPROM_PAGE_SIZE];
static uint8_t __aligned(32) read_buf[EEPROM_PAGE_SIZE];

ZTEST(i2c_eeprom, test_eeprom_write_read_verify)
{
	off_t offset = 0;
	const size_t eeprom_size = eeprom_get_size(eeprom);
	const uint8_t pattern_seed = (uint8_t)sys_rand32_get();

	TC_PRINT("Pseudo-random pattern seed: %u\n", pattern_seed);

	zassert_true(eeprom_size > 0U, "EEPROM size must be > 0");

	while (offset < eeprom_size) {
		size_t chunk_size = MIN(EEPROM_PAGE_SIZE, eeprom_size - offset);

		for (size_t i = 0; i < chunk_size; i++) {
			write_buf[i] = (uint8_t)((offset + i + pattern_seed) % 256U);
		}

		zassert_ok(eeprom_write(eeprom, offset, write_buf, chunk_size),
			   "EEPROM write failed at offset %zu", (size_t)offset);
		offset += chunk_size;
	}

	offset = 0;

	while (offset < eeprom_size) {
		size_t chunk_size = MIN(EEPROM_PAGE_SIZE, eeprom_size - offset);

		zassert_ok(eeprom_read(eeprom, offset, read_buf, chunk_size),
			   "EEPROM read failed at offset %zu", (size_t)offset);

		for (size_t i = 0; i < chunk_size; i++) {
			uint8_t expected = (uint8_t)((offset + i + pattern_seed) % 256U);

			zassert_equal(read_buf[i], expected,
				      "Data mismatch at addr %zu (exp=%u got=%u)",
				      (size_t)(offset + i), expected, read_buf[i]);
		}

		offset += chunk_size;
	}

	zassert_equal(offset, eeprom_size, "Verified bytes mismatch: %zu != %zu", (size_t)offset,
		      eeprom_size);
}

static void *eeprom_test_setup(void)
{
	zassert_true(device_is_ready(eeprom), "EEPROM device is not ready");
	return NULL;
}

ZTEST_SUITE(i2c_eeprom, NULL, eeprom_test_setup, NULL, NULL, NULL);
