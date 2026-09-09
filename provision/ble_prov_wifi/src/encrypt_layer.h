/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74XSYNC_ENCRYPT_LAYER_H_
#define QCC74XSYNC_ENCRYPT_LAYER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int qcc74xsync_encrypt_read(uint8_t packet_type, uint8_t *event_id,
				    uint8_t *dst, uint8_t *dst_len,
				    const uint8_t *src, size_t src_len,
				    bool first, size_t dst_capacity);

int qcc74xsync_encrypt_write(uint8_t packet_type, uint8_t event_id,
				     uint8_t *dst, uint8_t *dst_len,
				     const uint8_t *src, uint16_t src_len,
				     int16_t remaining, bool first);

#endif /* QCC74XSYNC_ENCRYPT_LAYER_H_ */
