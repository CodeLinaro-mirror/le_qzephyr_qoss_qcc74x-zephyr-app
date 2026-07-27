/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(rawfs_test, LOG_LEVEL_INF);

#define BLOCK_SIZE   (256) /* Min write block  and align boundary */
#define PATTERN_BYTE 0xA5  /* Pattern for write and verification */

static const struct flash_area *fa_raw = NULL;
static const struct flash_parameters *fp = NULL;
static uint8_t write_buf[BLOCK_SIZE];
static uint8_t read_buf[BLOCK_SIZE];

/* Callback function to print flash partition details */
static void print_partition(const struct flash_area *area, void *user_data)
{
	LOG_INF("Partition %s: offset=0x%lx, size=%u bytes",
		area->fa_dev->name ? area->fa_dev->name : "unknown", area->fa_off, area->fa_size);
}

static int flash_raw_init(void)
{
	int rc = 0;

	if (!fa_raw) {
		rc = flash_area_open(FIXED_PARTITION_ID(benchmarking_raw), &fa_raw);
		fp = flash_get_parameters(fa_raw->fa_dev);
		if (!fp) {
			LOG_ERR("Failed to get flash parameters for device %s",
				fa_raw->fa_dev->name);
			// flash_area_close(fa_raw);
		} else {
			LOG_INF("Flash write constraints for partition %s:", fa_raw->fa_dev->name);
			LOG_INF("  Minimal write size: %u bytes", fp->write_block_size);
			LOG_INF("  Write alignment: offset and length must be multiples of %u "
				"bytes",
				fp->write_block_size);
			LOG_INF("  Partition size: %u bytes", fa_raw->fa_size);
		}
	}
	return rc;
}

static int flash_raw_erase(uint32_t offset, uint32_t size)
{
	int ret = 0;

	size_t sector_size = 0x1000;

	/* do not erase if offset is in middle of section ! */
	uint32_t offset_roundup = ROUND_UP(offset, sector_size);

	uint32_t size_to_erase = 0;
	if (offset + size >= offset_roundup) {
		size_to_erase = ROUND_UP(size - (offset_roundup - offset), sector_size);
	} else if ((offset % sector_size) == 0) {
		size_to_erase = ROUND_UP(size, sector_size);
	} else {
		return 0;
	}

	/* Erase whole raw partition */
	ret = flash_area_erase(fa_raw, (off_t)offset_roundup, size_to_erase);

	return ret;
}

static int flash_raw_mount()
{
	int ret = 0;

	ret = flash_raw_init();
	if (!ret) {
		LOG_INF("RawFS erasing at once starts ...");
		ret = flash_raw_erase(0, fa_raw->fa_size);
	}

	return ret;
}

/* Main function */
int test_raw(void)
{

	int rc = 0;

	printk("\n");
	LOG_INF("Rawfs test started...");

	/* Print all available flash partitions */
	LOG_INF("Available flash partitions:");
	flash_area_foreach(print_partition, NULL);

	/* Mount raw partition */
	rc = flash_raw_mount();
	if (rc) {
		LOG_ERR("RawFS mount failed !");
		return rc;
	}

	LOG_INF("RawFS mounted successfully !");
	LOG_INF("RawFS erasing at once starts ...");

	/* Erase whole partition */
	rc = flash_raw_erase(0, fa_raw->fa_size);
	if (rc) {
		LOG_ERR("RawFS erase failed !");
		return rc;
	}

	LOG_INF("RawFS erased successfully !");

	/* Overwrite whole partition */
	uint32_t blocks = fa_raw->fa_size / BLOCK_SIZE;
	memset(write_buf, PATTERN_BYTE, BLOCK_SIZE);
	for (uint32_t i = 0; i < blocks; i++) {
		/* Write block */
		off_t offset = i * BLOCK_SIZE;
		rc = flash_area_write(fa_raw, offset, write_buf, BLOCK_SIZE);
		if (rc < 0) {
			LOG_ERR("Failed to write at offset 0x%lx: %d", offset, rc);
			return rc;
		}

		if (i % 1000 == 0 && i) {
			LOG_INF("%d blocks are written ...", i);
		}
	}

	LOG_INF("RawFS %d blocks ( bs %d bytes ) written successfully !", blocks, BLOCK_SIZE);

	/* Read and verify */
	for (uint32_t i = 0; i < blocks; i++) {
		/* Read block */
		off_t offset = i * BLOCK_SIZE;
		rc = flash_area_read(fa_raw, offset, read_buf, BLOCK_SIZE);
		if (rc < 0) {
			LOG_ERR("Failed to read at offset 0x%lx: %d", offset, rc);
			return rc;
		}
		/* Verify */
		for (size_t j = 0; j < BLOCK_SIZE; j++) {
			if (read_buf[j] != PATTERN_BYTE) {
				LOG_ERR("Verification failed at offset 0x%lx + %u: got 0x%02x, "
					"expected 0x%02x",
					offset, j, read_buf[j], PATTERN_BYTE);
				return -EIO;
			}
		}
	}

	LOG_INF("RawFS %d blocks ( bs %d bytes ) read and verified successfully !", blocks,
		BLOCK_SIZE);

	LOG_INF("Rawfs test ended !");

	return 0;
}