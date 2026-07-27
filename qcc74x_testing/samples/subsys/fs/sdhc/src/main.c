/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/time_units.h>
#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// Logging module
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Define the mount point for SD card
#define SD_MOUNT_POINT "/SD:"
#define DISK_NAME      "SD"

// Buffer size for write/read operations, min 512)
#define BUFFER_SIZE (32 * 1024)
static __attribute((aligned(CONFIG_SDHC_BUFFER_ALIGNMENT))) uint8_t buf[BUFFER_SIZE];

// Pattern seed for verification
#define PATTERN_SEED 0xAA

// Max file size for FAT32 (4GB - 1 byte)
// #define MAX_FILE_SIZE (4LL * 1024 * 1024 * 1024 - 1)
#define MAX_FILE_SIZE (1LL * 10 * 1024 * 1024)
// Create NUM files, set max to fill whole disk.
// #define FILE_NUM (0xFFFFFFF)
#define FILE_NUM      (5)

// Static FATFS structure
static FATFS fat_fs;

// Mount structure
static struct fs_mount_t sd_mp = {
	.type = FS_FATFS, .fs_data = &fat_fs, .mnt_point = SD_MOUNT_POINT};

// Check free space
static void check_free_space(void)
{
	struct fs_statvfs sbuf;
	int ret = fs_statvfs((const char *)SD_MOUNT_POINT, &sbuf);
	if (ret < 0) {
		LOG_ERR("Failed to fs_statvfs() %d", ret);
		return;
	}

	uint64_t free_bytes = (uint64_t)sbuf.f_bfree * sbuf.f_frsize;
	LOG_INF("Disk free space: %llu bytes", free_bytes);
	LOG_INF("Disk total space: %llu bytes", (uint64_t)sbuf.f_blocks * sbuf.f_frsize);
}

// Function to mount the SD card
static int mount_sd_card(void)
{
	int ret = fs_mount(&sd_mp);
	if (ret != 0) {
		LOG_ERR("Failed to mount SD card: %d", ret);
	} else {
		LOG_INF("SD card mounted successfully.");
	}
	return ret;
}

// Function to get disk capacity
static int64_t get_disk_capacity(void)
{
	uint32_t sector_count, sector_size;
	int ret = disk_access_init(DISK_NAME);
	if (ret != 0) {
		LOG_ERR("Disk init error: %d", ret);
		return -1;
	}
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret != 0) {
		LOG_ERR("Get sector count error: %d", ret);
		return -1;
	}
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret != 0) {
		LOG_ERR("Get sector size error: %d", ret);
		return -1;
	}
	LOG_INF("Disk capacity: %" PRIu32 " sectors, size %u bytes each. Total: %" PRIi64 " bytes",
		sector_count, sector_size, (int64_t)sector_count * sector_size);
	return (int64_t)sector_count * sector_size;
}

// Generate pattern for a buffer based on global position
static void generate_pattern(uint8_t *buf, size_t size, uint64_t position)
{
	for (size_t i = 0; i < size; i++) {
		buf[i] = (uint8_t)((position + i) ^ PATTERN_SEED);
	}
}

// Verify buffer against pattern
static bool verify_pattern(const uint8_t *buf, size_t size, uint64_t position)
{
	for (size_t i = 0; i < size; i++) {
		uint8_t expected = (uint8_t)((position + i) ^ PATTERN_SEED);
		if (buf[i] != expected) {
			LOG_ERR("Verification failed at position %" PRIu64
				": expected 0x%02x, got 0x%02x",
				position + i, expected, buf[i]);
			return false;
		}
	}
	return true;
}

// Function to write files to fill the disk
static int write_files_to_disk(void)
{
	struct fs_file_t file;
	int file_num = 1;
	uint64_t total_written = 0;
	bool disk_full = false;

	while (!disk_full) {
		char filename[32];
		snprintf(filename, sizeof(filename), "%s/test%d.dat", SD_MOUNT_POINT, file_num);

		fs_file_t_init(&file);
		int ret = fs_open(&file, filename, FS_O_CREATE | FS_O_WRITE);
		if (ret != 0) {
			LOG_ERR("Failed to open %s: %d", filename, ret);
			return ret;
		}

		LOG_INF("Writing %" PRIu64 " bytes to %s", MAX_FILE_SIZE, filename);

		// Start timing
		uint64_t generate_cycles = 0;
		uint64_t start_cycles = k_cycle_get_64();

		uint64_t file_written = 0;
		while (file_written < MAX_FILE_SIZE) {
			size_t to_write = BUFFER_SIZE;
			if (file_written + to_write > MAX_FILE_SIZE) {
				to_write = MAX_FILE_SIZE - file_written;
			}

			// Global position for pattern
			// Exclude pattern generation time from benchmark
			uint64_t generate_start = k_cycle_get_64();
			uint64_t position = (uint64_t)(file_num - 1) * MAX_FILE_SIZE + file_written;
			generate_pattern(buf, to_write, position);
			generate_cycles += (k_cycle_get_64() - generate_start);

			ssize_t written = fs_write(&file, buf, to_write);
			if (written < 0) {
				LOG_ERR("Write error: %d", (int)written);
				fs_close(&file);
				return (int)written;
			} else if (written == 0) {
				// Disk full
				disk_full = true;
				break;
			}

			file_written += written;
			total_written += written;

			if (written < to_write) {
				// Partial write, likely disk full
				disk_full = true;
				break;
			}
		}

		// End timing
		uint64_t end_cycles = k_cycle_get_64();

		ret = fs_sync(&file);
		if (ret < 0) {
			LOG_ERR("Failed fs_sync() %s: %d", filename, ret);
		}
		fs_close(&file);

		// Calculate speed in MB/s (1 MB = 1024 * 1024 bytes)
		uint64_t cycles = end_cycles - start_cycles - generate_cycles;
		uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
		double time_seconds = (double)cycles / freq_hz;
		double speed_kbps = (file_written / (1024.0)) / time_seconds;

		LOG_INF("Wrote %" PRIu64 " bytes to %s. Write speed %.2f KB/s", file_written,
			filename, speed_kbps);

		file_num++;

		if (file_num > FILE_NUM) {
			break;
		}
	}

	LOG_INF("Disk filled. Total written: %" PRIu64 " bytes, %d files", total_written,
		file_num - 1);

	return 0;
}

// Function to read and verify all files
static int read_and_verify_files(bool is_verify)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	int file_num = 1;
	uint64_t total_read = 0;

	while (true) {
		char filename[32];
		snprintf(filename, sizeof(filename), "%s/test%d.dat", SD_MOUNT_POINT, file_num);

		fs_file_t_init(&file);
		int ret = fs_open(&file, filename, FS_O_READ);
		if (ret != 0) {
			if (ret == -ENOENT) {
				// No more files
				break;
			}
			LOG_ERR("Failed to open %s, ret %d", filename, ret);
			return ret;
		}

		// Get file info (including size)
		ret = fs_stat(filename, &entry);
		if (ret < 0) {
			LOG_ERR("Failed to stat %s: %d", filename, ret);
			fs_close(&file);
			return ret;
		}

		if (is_verify) {
			LOG_INF("Read and verify %d bytes in %s", entry.size, filename);
		} else {
			LOG_INF("Read %d bytes in %s", entry.size, filename);
		}

		// Start timing
		uint64_t verify_cycles = 0;
		uint64_t start_cycles = k_cycle_get_64();

		uint64_t file_read = 0;
		while (true) {
			ssize_t read = fs_read(&file, buf, BUFFER_SIZE);
			if (read <= 0) {
				break; // End of file or error
			}

			if (is_verify) {
				// Global position for pattern
				// Exclude pattern generation time from benchmark
				uint64_t verify_start = k_cycle_get_64();
				uint64_t position =
					(uint64_t)(file_num - 1) * MAX_FILE_SIZE + file_read;
				if (!verify_pattern(buf, read, position)) {
					fs_close(&file);
					return -1;
				}
				verify_cycles += (k_cycle_get_64() - verify_start);
			}
			file_read += read;
			total_read += read;
		}

		// End timing
		uint64_t end_cycles = k_cycle_get_64();
		fs_close(&file);
		uint64_t cycles = end_cycles - start_cycles - verify_cycles;
		uint32_t freq_hz = sys_clock_hw_cycles_per_sec();
		double time_seconds = (double)cycles / freq_hz;

		// Calculate speed in MB/s (1 MB = 1024 * 1024 bytes)
		double speed_kbps = (file_read / (1024.0)) / time_seconds;

		if (is_verify) {
			LOG_INF("Verified %" PRIu64 " bytes in %s. Read speed %.2f KB/s", file_read,
				filename, speed_kbps);
		} else {
			LOG_INF("Read %" PRIu64 " bytes in %s. Read speed %.2f KB/s", file_read,
				filename, speed_kbps);
		}
		file_num++;
	}

	if (is_verify) {
		LOG_INF("Verification complete. Total verified: %" PRIu64 " bytes, %d files",
			total_read, file_num - 1);
	} else {
		LOG_INF("Read complete. Total read: %" PRIu64 " bytes, %d files", total_read,
			file_num - 1);
	}

	return 0;
}

int main(void)
{
	int ret = -1;
	LOG_INF("Starting SD card format, write, read and verify example.");

	// Get card capacity
	get_disk_capacity();

	while (1) {
		// Mount the SD card (format if necessary)
		if (mount_sd_card()) {
			break;
		}

		// Get capacity
		int64_t capacity = get_disk_capacity();
		LOG_INF("Disk capacity: %lld bytes, %lld Mbytes", capacity,
			capacity / (1024 * 1024));
		check_free_space();
		if (capacity < 0) {
			LOG_ERR("Unknown disk capacity.");
			break;
		}

		// Write files
		// Exclude pattern generation time from benchmark
		if (write_files_to_disk()) {
			LOG_ERR("Write files to disk failed.");
			break;
		}

		// Read and verify
		// Exclude pattern generation time from benchmark
		if (read_and_verify_files(true)) {
			LOG_ERR("Verification failed.");
			break;
		} else {
			LOG_INF("All operations completed successfully !");
			ret = 0;
			break;
		}
	}

	LOG_INF("All operations completed !");

	while (1) {
		k_msleep(2000);
	}

	return ret;
}
