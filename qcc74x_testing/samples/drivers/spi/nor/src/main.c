/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(w25q128_test, LOG_LEVEL_INF);

#define BUF_SIZE                     256
#define SPI_FLASH_TEST_REGION_OFFSET 0x00000
#define SPI_FLASH_SECTOR_SIZE        4096 /* W25Q128 sector size is 4KB */

static uint8_t __attribute((aligned(32))) write_buf[BUF_SIZE];
static uint8_t __attribute((aligned(32))) read_buf[BUF_SIZE];

int main(void)
{
	LOG_INF("Starting SPI NOR memory write, read, verify example.");

	/* Get the flash device (assuming 'spi-flash0' alias for W25Q128) */
	const struct device *const flash_dev = DEVICE_DT_GET(DT_ALIAS(spi_flash0));

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device %s not ready", flash_dev->name);
		return 0;
	}

	/* Detect flash size */
	struct flash_pages_info page_info;
	size_t size = 0;
	int ret = flash_get_page_info_by_offs(flash_dev, 0, &page_info);
	if (ret == 0) {
		size = page_info.size * flash_get_page_count(flash_dev);
		LOG_INF("Detected flash size: %zu bytes", size);
	} else {
		LOG_ERR("Failed to get flash size: %d", ret);
		return 0;
	}

	/* Limit test area */
	//size = 2 * SPI_FLASH_SECTOR_SIZE; // Temporary

	off_t offset = SPI_FLASH_TEST_REGION_OFFSET;

	/* Erase the required sectors before writing (W25Q128 requires erase) */
	LOG_INF("Starting erase operation...");
	size_t sectors_to_erase = (size + SPI_FLASH_SECTOR_SIZE - 1) / SPI_FLASH_SECTOR_SIZE;
	for (size_t i = 0; i < sectors_to_erase; i++) {
		ret = flash_erase(flash_dev, offset + (i * SPI_FLASH_SECTOR_SIZE),
				  SPI_FLASH_SECTOR_SIZE);
		if (ret != 0) {
			LOG_ERR("Flash erase failed at offset %ld: error %d",
				offset + (i * SPI_FLASH_SECTOR_SIZE), ret);
			return 0;
		}
	}
	LOG_INF("Erase operation complete.");

	/* Write data to flash in 64-byte chunks with a repeating pattern (0-255) */
	LOG_INF("Starting write operation...");
	offset = SPI_FLASH_TEST_REGION_OFFSET;
	while (offset < size) {
		size_t chunk_size = MIN(BUF_SIZE, size - offset);

		/* Fill write buffer with pattern based on offset */
		for (size_t i = 0; i < chunk_size; i++) {
			write_buf[i] = (uint8_t)((offset + i) % 256);
		}

		ret = flash_write(flash_dev, offset, write_buf, chunk_size);
		if (ret != 0) {
			LOG_ERR("Write failed at offset %ld: error %d", offset, ret);
			return 0;
		}

		offset += chunk_size;
	}

	LOG_INF("Write operation complete.");

	/* Read back and verify the data in 64-byte chunks */
	LOG_INF("Starting read and verify operation...");
	offset = SPI_FLASH_TEST_REGION_OFFSET;
	while (offset < size) {
		size_t chunk_size = MIN(BUF_SIZE, size - offset);

		ret = flash_read(flash_dev, offset, read_buf, chunk_size);
		if (ret != 0) {
			LOG_ERR("Read failed at offset %ld: error %d", offset, ret);
			return 0;
		}

		/* Verify the read data matches the written pattern */
		for (size_t i = 0; i < chunk_size; i++) {
			uint8_t expected = (uint8_t)((offset + i) % 256);
			if (read_buf[i] != expected) {
				LOG_ERR("Verification failed at address %ld: expected %u, got %u",
					offset + i, expected, read_buf[i]);
				return 0;
			}
		}

		offset += chunk_size;
	}

	LOG_INF("Read and verify operation successful. All data matches.");

	return 0;
}
