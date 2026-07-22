/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define PATH_SEPARATOR '/'

#include <bshg/filesystem.h>

#include <zephyr/fs/fs.h>
#include <libgen.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/syscalls/random.h>
#include <zephyr/fs/littlefs.h>

#include <BSHG_tracing.h>

// storage_partition (for littlefs)
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};

/**
 * Extracts the directory part of a given file path.
 * Returns the parent directory, or "." if no directory exists.
 */
void bshg_dirname(const jsmn_json_t *filepath, jsmn_json_t *dirpath)
{

	// create a temporary null-terminated string
	char temp_path[256] = {0};
	memcpy(temp_path, filepath->data, filepath->length);
	temp_path[sizeof(temp_path) - 1] = '\0';

	const char *last_slash = strrchr(filepath->data, PATH_SEPARATOR);
	if (last_slash) {
		size_t len = last_slash - (const char *)filepath->data; // Length of directory path
		strlcpy((void *)dirpath->data, temp_path, len + 1);
		dirpath->length = len;
	}
}

/**
 * Recursively creates directories from a given path.
 */
int create_directories(const jsmn_json_t *path)
{
	if (!path || !path->data || path->length == 0) {
		return -EINVAL; // Invalid argument
	}

	// create a temporary null-terminated string
	char temp_path[256] = {0};

	// Ensure the path is safely null-terminated
	if (path->length >= sizeof(temp_path)) {
		return -ENAMETOOLONG; // Path is too long
	}

	memcpy(temp_path, path->data, path->length);
	temp_path[path->length] = '\0'; // Null-terminate the copied path

	struct fs_dirent entry;
	if (fs_stat(temp_path, &entry) == 0) {
		return 0; // Directory already exists
	}

	// Iterate through the path, creating directories step-by-step
	for (char *p = temp_path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0'; // Temporarily terminate at this subdirectory
			if (fs_stat(temp_path, &entry) != 0) {
				fs_mkdir(temp_path); // Create the directory
			}
			*p = '/'; // Restore original path separator
		}
	}

	return fs_mkdir(temp_path); // Finally, create the full path
}

/**
 * Writes content to a file, creating missing directories if needed.
 */
int generate_file(const jsmn_json_t *path, uint32_t size)
{
	// ensure a null-terminated string
	char path_null_terminated[256] = {0};
	size_t path_copy_len = MIN(path->length, sizeof(path_null_terminated) - 1);
	memcpy(path_null_terminated, path->data, path_copy_len);
	path_null_terminated[path_copy_len] = '\0';

	struct fs_file_t file;
	fs_file_t_init(&file);

	// Create directory
	char dirpath_buffer[256];
	jsmn_json_t dirpath = {dirpath_buffer, sizeof(dirpath_buffer)};
	bshg_dirname(path, &dirpath);
	create_directories(&dirpath);

	int ret = 0;

	// remove file first if already existing
	remove_file(path);

	ret = fs_open(&file, path_null_terminated, FS_O_CREATE | FS_O_APPEND | FS_O_RDWR);
	if (ret < 0) {
		return ret; // Failed to open file
	}

	// Generate random data
	char chunk[1000] = {0};
	uint32_t chunk_size = 0;
	uint32_t written_bytes = 0;
	uint32_t bytes_left = size;

	while (bytes_left > 0) {
		chunk_size = MIN(bytes_left, sizeof(chunk));
		sys_rand_get(chunk, chunk_size);
		ret = fs_write(&file, &chunk, chunk_size);
		if (ret < 0) {
			break;
		}
		written_bytes += ret;
		bytes_left -= chunk_size;
	}

	fs_truncate(&file, (off_t)written_bytes);
	fs_sync(&file);
	fs_close(&file);

	if (ret >= 0) {
		ret = (int)written_bytes;
	}

	return ret;
}

/**
 * Writes content to a file, creating missing directories if needed.
 */
int write_file(const jsmn_json_t *path, const jsmn_json_t *content)
{
	struct fs_file_t file;
	fs_file_t_init(&file);

	char dirpath_buffer[256];
	jsmn_json_t dirpath = {dirpath_buffer, sizeof(dirpath_buffer)};

	bshg_dirname(path, &dirpath);
	create_directories(&dirpath);

	// ensure a null-terminated string
	char path_null_terminated[256] = {0};
	size_t path_copy_len = MIN(path->length, sizeof(path_null_terminated) - 1);
	memcpy(path_null_terminated, path->data, path_copy_len);
	path_null_terminated[path_copy_len] = '\0';

	int ret = 0;

	ret = fs_open(&file, path_null_terminated, FS_O_CREATE | FS_O_RDWR);
	if (ret < 0) {
		return ret; // Failed to open file
	}

	ret = fs_write(&file, content->data, content->length);
	if (ret < 0) {
		return ret; // Failed to write file
	}
	fs_truncate(&file, content->length);
	fs_sync(&file);
	fs_close(&file);
	return ret;
}

/**
 * Append content to an existing file
 */
int append_file(const jsmn_json_t *path, const jsmn_json_t *content)
{
	struct fs_file_t file;
	fs_file_t_init(&file);

	// ensure a null-terminated string
	char path_null_terminated[256] = {0};
	size_t path_copy_len = MIN(path->length, sizeof(path_null_terminated) - 1);
	memcpy(path_null_terminated, path->data, path_copy_len);
	path_null_terminated[path_copy_len] = '\0';

	uint32_t filesize = get_filesize(path_null_terminated);
	if (filesize < 0) {
		return -1; // File does not exist
	}

	int ret = fs_open(&file, path_null_terminated, FS_O_CREATE | FS_O_APPEND | FS_O_RDWR);
	if (ret < 0) {
		return ret; // Failed to open file
	}

	ret = fs_write(&file, content->data, content->length);
	if (ret < 0) {
		return ret; // Failed to write file
	}

	fs_truncate(&file, filesize + content->length);
	fs_sync(&file);
	fs_close(&file);
	return ret;
}

int32_t get_filesize(const char *path)
{
	struct fs_dirent entry;
	if (fs_stat(path, &entry)) {
		return -1; // File does not exist
	}
	return entry.size;
}

int read_file(const jsmn_json_t *path, jsmn_json_t *result)
{
	struct fs_file_t file;
	fs_file_t_init(&file);

	// ensure a null-terminated string
	char path_null_terminated[256] = {0};
	size_t path_copy_len = MIN(path->length, sizeof(path_null_terminated) - 1);
	memcpy(path_null_terminated, path->data, path_copy_len);
	path_null_terminated[path_copy_len] = '\0';

	uint32_t filesize = get_filesize(path_null_terminated);
	if (filesize <= 0) {
		return -1; // File is empty or error in getting size
	}

	int ret = fs_open(&file, path_null_terminated, FS_O_READ);
	if (ret < 0) {
		return ret; // Failed to open file
	}

	char chunk[256] = {0};
	ssize_t len = (ssize_t)filesize;
	while (len > 0) {
		len -= fs_read(&file, chunk, sizeof(chunk) - 1);
	}
	if (len < 0) // Read file into buffer
	{
		return -1; // Failed to read file
	}

	fs_close(&file);

	if (filesize <= result->length) {
		memcpy((void *)result->data, chunk, filesize);
		result->length = filesize;
	} else {
		char too_big[] = "<file is too big>";
		memcpy((void *)result->data, too_big, sizeof(too_big));
		result->length = sizeof(too_big);
	}

	ret = (int)filesize;
	return ret;
}

int remove_file(const jsmn_json_t *path)
{
	// ensure a null-terminated string
	char path_null_terminated[256] = {0};
	size_t path_copy_len = MIN(path->length, sizeof(path_null_terminated) - 1);
	memcpy(path_null_terminated, path->data, path_copy_len);
	path_null_terminated[path_copy_len] = '\0';
	int retval = 0;

	if (get_filesize(path_null_terminated) > 0) {
		retval = fs_unlink(path_null_terminated);
	} else {
		retval = -1; // File does not exist
	}

	return retval;
}

void mount_littlefs()
{
	struct fs_dirent entry;
	int not_mounted = fs_stat("/lfs", &entry);
	if (not_mounted) {
		fs_mount(&lfs_storage_mnt);
	}
}