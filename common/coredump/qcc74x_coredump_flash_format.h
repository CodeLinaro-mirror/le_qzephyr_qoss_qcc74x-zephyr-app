/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74X_COREDUMP_FLASH_FORMAT_H_
#define QCC74X_COREDUMP_FLASH_FORMAT_H_

#include <stdint.h>

#define QCC74X_COREDUMP_FLASH_MAGIC 0x51434446u
#define QCC74X_COREDUMP_FLASH_VERSION 1u
#define QCC74X_COREDUMP_FLASH_STATE_PREPARED 0xfffffffEu
#define QCC74X_COREDUMP_FLASH_STATE_VALID 0xfffffffCu

struct qcc74x_coredump_flash_header {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t state;
	uint32_t payload_length;
	uint32_t payload_crc;
	uint32_t reserved[2];
};

#endif /* QCC74X_COREDUMP_FLASH_FORMAT_H_ */
