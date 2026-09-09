/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "encrypt_layer.h"

#include <errno.h>
#include <string.h>

#include "payload.h"

int qcc74xsync_encrypt_read(uint8_t packet_type, uint8_t *event_id,
				    uint8_t *dst, uint8_t *dst_len,
				    const uint8_t *src, size_t src_len,
				    bool first, size_t dst_capacity)
{
	/* qcc74xsync reserves nine bytes on the first fragment and one byte on
	 * continuation fragments. These fields are compatibility framing only; the
	 * current implementation does not decrypt or authenticate the payload. */
	if (first) {
		if (src_len < 9U) {
			return -EINVAL;
		}
		src += 9U;
		src_len -= 9U;
	} else {
		if (src_len < 1U) {
			return -EINVAL;
		}
		src++;
		src_len--;
	}

	return qcc74xsync_payload_read(packet_type, event_id, dst, dst_len,
					       src, src_len, dst_capacity);
}

int qcc74xsync_encrypt_write(uint8_t packet_type, uint8_t event_id,
				     uint8_t *dst, uint8_t *dst_len,
				     const uint8_t *src, uint16_t src_len,
				     int16_t remaining, bool first)
{
	uint8_t framing_length = first ? 9U : 1U;

	if (remaining < framing_length) {
		return -EINVAL;
	}

	memset(dst, 0, framing_length);
	*dst_len += framing_length;
	remaining -= framing_length;

	return qcc74xsync_payload_write(packet_type, event_id,
					&dst[framing_length], dst_len, src, src_len,
					remaining);
}
