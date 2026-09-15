/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef QCC74X_FOTA_OTA_INTERNAL_H_
#define QCC74X_FOTA_OTA_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#define QCC74X_OTA_HEADER_SIZE 512U

struct __packed qcc74x_ota_header {
	char magic[16];
	char type[4];
	uint32_t length;
	uint8_t reserved[8];
	uint8_t hardware_version[16];
	uint8_t software_version[16];
	uint8_t sha256[32];
	uint8_t padding[416];
};

struct qcc74x_ota_image {
	uint32_t address;
	uint32_t capacity;
	uint8_t active_slot;
};

int qcc74x_ota_prepare(struct qcc74x_ota_image *image);
int qcc74x_ota_validate_header(const struct qcc74x_ota_header *header,
			       const struct qcc74x_ota_image *image);
void qcc74x_ota_stage_begin(const struct qcc74x_ota_header *header);
int qcc74x_ota_write(const struct qcc74x_ota_image *image, uint32_t offset, const uint8_t *data,
		     uint32_t length);
int qcc74x_ota_verify(const struct qcc74x_ota_image *image, const struct qcc74x_ota_header *header,
		      uint8_t *buffer, size_t buffer_size);
void qcc74x_ota_set_error(int error);

#endif
