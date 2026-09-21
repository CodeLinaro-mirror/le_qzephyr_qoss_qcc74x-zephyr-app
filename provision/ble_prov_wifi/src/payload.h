/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74XSYNC_PAYLOAD_H_
#define QCC74XSYNC_PAYLOAD_H_

#include <stddef.h>
#include <stdint.h>

enum qcc74xsync_packet_type {
	QCC74XSYNC_PACKET_CMD = 0U,
	QCC74XSYNC_PACKET_DATA = 1U,
};

int qcc74xsync_payload_read(uint8_t packet_type, uint8_t *event_id,
				    uint8_t *dst, uint8_t *dst_len,
				    const uint8_t *src, size_t src_len,
				    size_t dst_capacity);

int qcc74xsync_payload_write(uint8_t packet_type, uint8_t event_id,
				     uint8_t *dst, uint8_t *dst_len,
				     const uint8_t *src, uint16_t src_len,
				     int16_t remaining);

#endif /* QCC74XSYNC_PAYLOAD_H_ */
