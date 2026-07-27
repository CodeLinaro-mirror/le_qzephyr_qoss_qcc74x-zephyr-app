/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(littlefs_test, LOG_LEVEL_INF);

/* Configuration */
#define MAX_PATH_LEN  255
#define FILE_SIZE     (6 * 1024) /* 6KB */
#define MOUNT_POINT   "/lfs"
#define THREAD1_DIR   MOUNT_POINT "/thread1"
#define THREAD2_DIR   MOUNT_POINT "/thread2"
#define PATTERN_BYTE  0xA5 /* Pattern for file content verification */
#define PATTERN1_BYTE 0x5A /* Pattern for file content verification */

/* File system configuration */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = MOUNT_POINT,
};

/* Callback function to print flash partition details */
static void print_partition(const struct flash_area *area, void *user_data)
{
	LOG_INF("Partition %s: offset=0x%lx, size=%u bytes",
		area->fa_dev->name ? area->fa_dev->name : "unknown", area->fa_off, area->fa_size);
}

/* Function to list all files in a given path */
static int list_files(const char *path)
{
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	int rc;

	fs_dir_t_init(&dirp);
	rc = fs_opendir(&dirp, path);
	if (rc < 0) {
		LOG_ERR("Failed to open directory %s: %d", path, rc);
		return rc;
	}

	LOG_INF("Files in %s:", path);
	while (true) {
		rc = fs_readdir(&dirp, &entry);
		if (rc < 0) {
			LOG_ERR("Error reading directory %s: %d", path, rc);
			fs_closedir(&dirp);
			return rc;
		}
		if (entry.name[0] == 0) {
			break; /* End of directory */
		}
		if (entry.type == FS_DIR_ENTRY_FILE) {
			LOG_INF("  %s", entry.name);
		}
	}

	fs_closedir(&dirp);
	return 0;
}

/* Function to list all directories in a given path */
static int list_directories(const char *path)
{
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	int rc;

	fs_dir_t_init(&dirp);
	rc = fs_opendir(&dirp, path);
	if (rc < 0) {
		LOG_ERR("Failed to open directory %s: %d", path, rc);
		return rc;
	}

	LOG_INF("Directories in %s:", path);
	while (true) {
		rc = fs_readdir(&dirp, &entry);
		if (rc < 0) {
			LOG_ERR("Error reading directory %s: %d", path, rc);
			fs_closedir(&dirp);
			return rc;
		}
		if (entry.name[0] == 0) {
			break; /* End of directory */
		}
		if (entry.type == FS_DIR_ENTRY_DIR) {
			LOG_INF("  %s", entry.name);
		}
	}

	fs_closedir(&dirp);

	return 0;
}

/* Function to create a directory if it doesn't exist */
static int create_directory(const char *path)
{
	struct fs_dir_t dir;
	int rc;

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, path);
	if (rc == -ENOENT) {
		rc = fs_mkdir(path);
		if (rc < 0) {
			LOG_ERR("Failed to create directory %s: %d", path, rc);
			return rc;
		}
		LOG_INF("Created directory %s", path);
	} else if (rc < 0) {
		LOG_ERR("Error opening directory %s: %d", path, rc);
		fs_closedir(&dir);
		return rc;
	}
	fs_closedir(&dir);
	return 0;
}

/* Function to create a file with a pattern and verify its content */
static int create_and_verify_file(const char *dir, int file_num, uint8_t pattern)
{
	static char fname[MAX_PATH_LEN];
	struct fs_file_t file;
	static uint8_t write_buf[FILE_SIZE];
	static uint8_t read_buf[FILE_SIZE];
	int rc;

	/* Initialize file */
	fs_file_t_init(&file);
	snprintf(fname, sizeof(fname), "%s/file_%d.bin", dir, file_num);

	/* Fill write buffer with pattern */
	memset(write_buf, pattern, FILE_SIZE);

	/* Create and write file */
	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		LOG_ERR("Failed to open %s for writing: %d", fname, rc);
		return rc;
	}

	rc = fs_write(&file, write_buf, FILE_SIZE);
	if (rc != FILE_SIZE) {
		LOG_ERR("Failed to write %s: wrote %d bytes, expected %d, error %d", fname, rc,
			FILE_SIZE, rc < 0 ? rc : 0);
		fs_close(&file);
		return rc < 0 ? rc : -EIO;
	}

	rc = fs_close(&file);
	if (rc < 0) {
		LOG_ERR("Failed to close %s after writing: %d", fname, rc);
		return rc;
	}

	/* Verify file content */
	rc = fs_open(&file, fname, FS_O_READ);
	if (rc < 0) {
		LOG_ERR("Failed to open %s for reading: %d", fname, rc);
		return rc;
	}

	rc = fs_read(&file, read_buf, FILE_SIZE);
	if (rc != FILE_SIZE) {
		LOG_ERR("Failed to read %s: read %d bytes, expected %d, error %d", fname, rc,
			FILE_SIZE, rc < 0 ? rc : 0);
		fs_close(&file);
		return rc < 0 ? rc : -EIO;
	}

	for (int i = 0; i < FILE_SIZE; i++) {
		if (read_buf[i] != pattern) {
			LOG_ERR("Verification failed for %s at byte %d: got 0x%02x, expected "
				"0x%02x",
				fname, i, read_buf[i], pattern);
			fs_close(&file);
			return -EIO;
		}
	}

	rc = fs_close(&file);
	if (rc < 0) {
		LOG_ERR("Failed to close %s after reading: %d", fname, rc);
		return rc;
	}

	LOG_INF("Created and verified %s", fname);

	return 0;
}

/* Function to delete all files in a directory */
static int delete_all_files(const char *dir)
{
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	static char fname[MAX_PATH_LEN + 16];
	int rc;

	fs_dir_t_init(&dirp);
	rc = fs_opendir(&dirp, dir);
	if (rc < 0) {
		LOG_ERR("Failed to open directory %s: %d", dir, rc);
		return rc;
	}

	while (true) {
		rc = fs_readdir(&dirp, &entry);
		if (rc < 0) {
			LOG_ERR("Error reading directory %s: %d", dir, rc);
			fs_closedir(&dirp);
			return rc;
		}
		if (entry.name[0] == 0) {
			break; /* End of directory */
		}
		if (entry.type != FS_DIR_ENTRY_FILE) {
			continue; /* Skip subdirectories */
		}

		snprintf(fname, sizeof(fname), "%s/%s", dir, entry.name);
		rc = fs_unlink(fname);
		if (rc < 0) {
			LOG_ERR("Failed to delete %s: %d", fname, rc);
			fs_closedir(&dirp);
			return rc;
		}
		LOG_INF("Deleted %s", fname);
	}

	fs_closedir(&dirp);

	return 0;
}

/* Main function */
int test_littlefs(void)
{
	int rc;

	LOG_INF("Littlefs test started...");

	k_sleep(K_MSEC(50));

	/* Print all available flash partitions */
	LOG_INF("Available flash partitions:");
	flash_area_foreach(print_partition, NULL);

	/* Mount LittleFS */
	struct fs_dirent entry;
	int not_mounted = fs_stat(MOUNT_POINT, &entry);
	if (not_mounted) {
		rc = fs_mount(&lfs_mnt);
		if (rc < 0) {
			LOG_ERR("Failed to mount LittleFS: %d", rc);
			return rc;
		}
	}
	LOG_INF("LittleFS mounted successfully at %s", MOUNT_POINT);

	/* Create thread directories */
	rc = create_directory(THREAD1_DIR);
	if (rc < 0) {
		return rc;
	}
	rc = create_directory(THREAD2_DIR);
	if (rc < 0) {
		return rc;
	}

	/* List all directories in the mount point */
	rc = list_directories(MOUNT_POINT);
	if (rc < 0) {
		LOG_ERR("Failed to list directories at %s: %d", MOUNT_POINT, rc);
	}

	struct fs_statvfs sbuf;
	rc = fs_statvfs(MOUNT_POINT, &sbuf);
	if (rc >= 0) {
		LOG_INF("%s: bsize = %lu ; frsize = %lu ;"
			" blocks = %lu ; bfree = %lu",
			MOUNT_POINT, sbuf.f_bsize, sbuf.f_frsize, sbuf.f_blocks, sbuf.f_bfree);
	}

	k_sleep(K_MSEC(50));

	create_and_verify_file((void *)THREAD1_DIR, 0, PATTERN_BYTE);
	create_and_verify_file((void *)THREAD2_DIR, 0, PATTERN1_BYTE);
	create_and_verify_file((void *)THREAD1_DIR, 1, PATTERN_BYTE);
	create_and_verify_file((void *)THREAD2_DIR, 1, PATTERN1_BYTE);
	create_and_verify_file((void *)THREAD1_DIR, 77, PATTERN_BYTE);
	create_and_verify_file((void *)THREAD2_DIR, 77, PATTERN1_BYTE);
	create_and_verify_file((void *)THREAD1_DIR, 200, PATTERN_BYTE);
	create_and_verify_file((void *)THREAD2_DIR, 200, PATTERN1_BYTE);
	create_and_verify_file((void *)THREAD1_DIR, 999, PATTERN_BYTE);
	create_and_verify_file((void *)THREAD2_DIR, 999, PATTERN1_BYTE);

	k_sleep(K_MSEC(100));

	/* List all files */
	rc = list_files(THREAD1_DIR);
	if (rc < 0) {
		LOG_ERR("Failed to list files at %s: %d", THREAD1_DIR, rc);
	}

	/* List all files */
	rc = list_directories(THREAD2_DIR);
	if (rc < 0) {
		LOG_ERR("Failed to list files at %s: %d", THREAD2_DIR, rc);
	}

	k_sleep(K_MSEC(100));

	rc = delete_all_files(THREAD1_DIR);
	if (rc < 0) {
		LOG_ERR("Dir %s: Error deleting files: %d", THREAD1_DIR, rc);
	}

	/* List all files */
	rc = list_files(THREAD1_DIR);
	if (rc < 0) {
		LOG_ERR("Failed to list files at %s: %d", THREAD1_DIR, rc);
	}

	/* List all directories */
	rc = list_files(THREAD2_DIR);
	if (rc < 0) {
		LOG_ERR("Failed to list files at %s: %d", THREAD2_DIR, rc);
	}

	LOG_INF("Littlefs test ended !");

	return 0;
}
