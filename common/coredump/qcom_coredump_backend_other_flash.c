/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash/qcc74x_emergency.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "qcc74x_coredump_flash_format.h"

#define QCC74X_COREDUMP_FLASH_STORAGE_NODE DT_PARENT(DT_PARENT(DT_NODELABEL(coredump_partition)))
#define QCC74X_COREDUMP_FLASH_DEVICE_NODE DT_PARENT(QCC74X_COREDUMP_FLASH_STORAGE_NODE)
#define QCC74X_COREDUMP_FLASH_SLOT_OFFSET DT_REG_ADDR(DT_NODELABEL(coredump_partition))
#define QCC74X_COREDUMP_FLASH_SLOT_SIZE DT_REG_SIZE(DT_NODELABEL(coredump_partition))
#define QCC74X_COREDUMP_FLASH_WRITE_BLOCK DT_PROP(QCC74X_COREDUMP_FLASH_STORAGE_NODE, write_block_size)
#define QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET QCC74X_COREDUMP_FLASH_WRITE_BLOCK
#define QCC74X_COREDUMP_FLASH_BUFFER_SIZE (QCC74X_COREDUMP_FLASH_WRITE_BLOCK * 8U)

BUILD_ASSERT(QCC74X_COREDUMP_FLASH_WRITE_BLOCK > 0U);
BUILD_ASSERT(QCC74X_COREDUMP_FLASH_SLOT_SIZE > QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET);

BUILD_ASSERT(sizeof(struct qcc74x_coredump_flash_header) <=
	     QCC74X_COREDUMP_FLASH_WRITE_BLOCK);

static struct flash_qcc74x_emergency_session coredump_flash_session;
static uint8_t coredump_flash_buffer[QCC74X_COREDUMP_FLASH_BUFFER_SIZE];
static size_t coredump_flash_buffer_length;
static size_t coredump_flash_payload_length;
static size_t coredump_flash_written_length;
static uint32_t coredump_flash_payload_crc;
static int coredump_flash_error;

static int qcom_coredump_flash_flush_buffer(void)
{
	int result;

	if (coredump_flash_buffer_length == 0U) {
		return 0;
	}

	if ((coredump_flash_written_length + coredump_flash_buffer_length) >
	    (QCC74X_COREDUMP_FLASH_SLOT_SIZE - QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET)) {
		return -ENOSPC;
	}

	result = flash_qcc74x_emergency_write(&coredump_flash_session,
		QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET + coredump_flash_written_length,
		coredump_flash_buffer, coredump_flash_buffer_length);
	if (result != 0) {
		return result;
	}

	coredump_flash_written_length += coredump_flash_buffer_length;
	coredump_flash_buffer_length = 0U;
	return 0;
}

static int qcom_coredump_flash_write_header(uint32_t state)
{
	struct qcc74x_coredump_flash_header header = {
		.magic = QCC74X_COREDUMP_FLASH_MAGIC,
		.version = QCC74X_COREDUMP_FLASH_VERSION,
		.header_size = sizeof(header),
		.state = state,
		.payload_length = coredump_flash_payload_length,
		.payload_crc = coredump_flash_payload_crc,
	};
	uint8_t write_block[QCC74X_COREDUMP_FLASH_WRITE_BLOCK];

	memset(write_block, 0xff, sizeof(write_block));
	memcpy(write_block, &header, sizeof(header));
	return flash_qcc74x_emergency_write(&coredump_flash_session, 0,
		write_block, sizeof(write_block));
}

static void qcom_coredump_other_flash_start(void)
{
	const struct flash_qcc74x_emergency_region region = {
		.offset = QCC74X_COREDUMP_FLASH_SLOT_OFFSET,
		.size = QCC74X_COREDUMP_FLASH_SLOT_SIZE,
	};
	const struct device *flash = DEVICE_DT_GET(QCC74X_COREDUMP_FLASH_DEVICE_NODE);

	coredump_flash_buffer_length = 0U;
	coredump_flash_payload_length = 0U;
	coredump_flash_written_length = 0U;
	coredump_flash_payload_crc = 0U;
	coredump_flash_error = 0;
	memset(&coredump_flash_session, 0, sizeof(coredump_flash_session));

	if (!device_is_ready(flash)) {
		coredump_flash_error = -ENODEV;
		return;
	}

	coredump_flash_error = flash_qcc74x_emergency_try_lock(flash, &region,
							&coredump_flash_session);
	if (coredump_flash_error == 0) {
		coredump_flash_error = flash_qcc74x_emergency_erase(&coredump_flash_session);
	}
}

static void qcom_coredump_other_flash_buffer_output(uint8_t *buffer, size_t length)
{
	if (coredump_flash_error != 0 || buffer == NULL || length == 0U) {
		if (buffer == NULL || length == 0U) {
			coredump_flash_error = -EINVAL;
		}
		return;
	}

	while (length > 0U) {
		size_t copy_length = MIN(length, sizeof(coredump_flash_buffer) -
							  coredump_flash_buffer_length);

		/*
		 * The full-memory profile can emit a range containing this backend's
		 * staging buffer.  Snapshot with memmove(), then checksum the exact
		 * bytes that will be passed to the emergency Flash writer.
		 */
		memmove(&coredump_flash_buffer[coredump_flash_buffer_length], buffer,
			copy_length);
		coredump_flash_buffer_length += copy_length;
		coredump_flash_payload_length += copy_length;
		coredump_flash_payload_crc = crc32_ieee_update(coredump_flash_payload_crc,
							       &coredump_flash_buffer[coredump_flash_buffer_length -
										    copy_length],
							       copy_length);
		buffer += copy_length;
		length -= copy_length;

		if (coredump_flash_buffer_length == sizeof(coredump_flash_buffer)) {
			coredump_flash_error = qcom_coredump_flash_flush_buffer();
			if (coredump_flash_error != 0) {
				return;
			}
		}
	}
}

static void qcom_coredump_other_flash_end(void)
{
	if (coredump_flash_error == 0 && coredump_flash_buffer_length != 0U) {
		size_t payload_bytes = coredump_flash_buffer_length;
		size_t padded_length = ROUND_UP(payload_bytes, QCC74X_COREDUMP_FLASH_WRITE_BLOCK);

		memset(&coredump_flash_buffer[payload_bytes], 0xff,
		       padded_length - payload_bytes);
		coredump_flash_buffer_length = padded_length;
		coredump_flash_error = qcom_coredump_flash_flush_buffer();
	}

	if (coredump_flash_error == 0) {
		coredump_flash_error = qcom_coredump_flash_write_header(
			QCC74X_COREDUMP_FLASH_STATE_PREPARED);
	}
	if (coredump_flash_error == 0) {
		coredump_flash_error = qcom_coredump_flash_write_header(
			QCC74X_COREDUMP_FLASH_STATE_VALID);
	}

	flash_qcc74x_emergency_unlock(&coredump_flash_session);
}

static int qcom_coredump_other_flash_query(enum coredump_query_id query_id, void *arg)
{
	ARG_UNUSED(arg);
	return query_id == COREDUMP_QUERY_GET_ERROR ? coredump_flash_error : -ENOTSUP;
}

static int qcom_coredump_other_flash_cmd(enum coredump_cmd_id cmd_id, void *arg)
{
	ARG_UNUSED(arg);
	if (cmd_id == COREDUMP_CMD_CLEAR_ERROR) {
		coredump_flash_error = 0;
		return 0;
	}
	return -ENOTSUP;
}

struct coredump_backend_api coredump_backend_other = {
	.start = qcom_coredump_other_flash_start,
	.end = qcom_coredump_other_flash_end,
	.buffer_output = qcom_coredump_other_flash_buffer_output,
	.query = qcom_coredump_other_flash_query,
	.cmd = qcom_coredump_other_flash_cmd,
};
