/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* QCC74x FOTA LittleFS transport. The OTA image format is unchanged. */
#include "../ota/ota_internal.h"
#include "qcc74x_fota.h"

#include <errno.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

/* qcc74x_ota_verify() reuses this buffer for FLASH_BLOCK-sized SHA reads. */
#define FILE_IO_BLOCK 4096U

static uint8_t file_io[FILE_IO_BLOCK];

static int read_exact(struct fs_file_t *file, void *buffer, size_t length)
{
	uint8_t *cursor = buffer;
	ssize_t rc;

	while (length != 0U) {
		rc = fs_read(file, cursor, length);
		if (rc < 0) {
			return (int)rc;
		}
		if (rc == 0) {
			return -EBADMSG;
		}

		cursor += rc;
		length -= (size_t)rc;
	}

	return 0;
}

int qcc74x_fota_stage_file(const char *path)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	struct qcc74x_ota_header header;
	struct qcc74x_ota_image image;
	uint32_t offset = 0U;
	int rc;

	if (path == NULL) {
		return -EINVAL;
	}

	printk("OTA file: entering local-file transport\n");
	printk("OTA file: stat %s\n", path);
	rc = fs_stat(path, &entry);
	if (rc < 0) {
		return rc;
	}
	if (entry.type != FS_DIR_ENTRY_FILE || entry.size < QCC74X_OTA_HEADER_SIZE) {
		return -EINVAL;
	}

	printk("OTA file: found %u bytes; opening\n", (uint32_t)entry.size);
	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_READ);
	if (rc < 0) {
		return rc;
	}

	printk("OTA file: reading header\n");
	rc = read_exact(&file, &header, sizeof(header));
	if (rc < 0) {
		goto out;
	}

	/* Reject a truncated fixture before erasing the inactive FW slot. */
	if (entry.size != QCC74X_OTA_HEADER_SIZE + header.length) {
		rc = -EBADMSG;
		goto out;
	}

	printk("OTA file: header complete; preparing inactive slot\n");
	rc = qcc74x_ota_prepare(&image);
	if (rc < 0) {
		goto out;
	}

	rc = qcc74x_ota_validate_header(&header, &image);
	if (rc < 0) {
		goto out;
	}

	qcc74x_ota_stage_begin(&header);
	printk("OTA file: staging %u bytes from %s\n", header.length, path);

	while (offset < header.length) {
		uint32_t length = MIN((uint32_t)sizeof(file_io), header.length - offset);

		rc = read_exact(&file, file_io, length);
		if (rc < 0) {
			goto out;
		}

		rc = qcc74x_ota_write(&image, offset, file_io, length);
		if (rc < 0) {
			goto out;
		}

		offset += length;
		k_yield();
	}

	printk("OTA file: verifying SHA-256\n");
	rc = qcc74x_ota_verify(&image, &header, file_io, sizeof(file_io));

out:
	(void)fs_close(&file);
	qcc74x_ota_set_error(rc);
	return rc;
}
