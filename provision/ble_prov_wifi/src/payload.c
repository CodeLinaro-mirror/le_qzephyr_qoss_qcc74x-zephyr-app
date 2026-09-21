/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "payload.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

int qcc74xsync_payload_read(uint8_t packet_type, uint8_t *event_id,
				    uint8_t *dst, uint8_t *dst_len,
				    const uint8_t *src, size_t src_len,
				    size_t dst_capacity)
{
	if (packet_type == QCC74XSYNC_PACKET_CMD) {
		/* The legacy APK can omit the final reserved command byte. */
		if ((src_len != 4U && src_len != 5U) || dst_capacity < 4U) {
			return -EINVAL;
		}

		*dst_len = 4U;
		*event_id = src[0];
		memset(dst, 0, 4U);
		memcpy(dst, &src[1], src_len - 1U);
		return 0;
	}

	if (src_len < 2U || src[0] != src_len - 2U || dst_capacity < src[0]) {
		return -EINVAL;
	}

	*dst_len = src[0];
	*event_id = src[1];
	memcpy(dst, &src[2], *dst_len);
	return 0;
}

int qcc74xsync_payload_write(uint8_t packet_type, uint8_t event_id,
				     uint8_t *dst, uint8_t *dst_len,
				     const uint8_t *src, uint16_t src_len,
				     int16_t remaining)
{
	uint16_t payload_size;

	if (remaining < 0) {
		return -EINVAL;
	}

	if (packet_type == QCC74XSYNC_PACKET_CMD) {
		if (remaining < 5 || src_len < 4U) {
			return -EINVAL;
		}

		dst[0] = event_id;
		memcpy(&dst[1], src, 4U);
		*dst_len += 5U;
		return 4;
	}

	if (remaining < 2) {
		return -EINVAL;
	}

	payload_size = MIN((uint16_t)(remaining - 2), src_len);
	dst[0] = (uint8_t)payload_size;
	dst[1] = event_id;
	memcpy(&dst[2], src, payload_size);
	*dst_len += 2U + payload_size;
	return payload_size;
}
