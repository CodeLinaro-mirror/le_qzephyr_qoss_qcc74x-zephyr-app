/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "transfer.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "encrypt_layer.h"
#include "payload.h"

#define CTRL_RETRY_BIT 0U
#define CTRL_FRAG_BIT 1U
#define CTRL_ACK_BIT 3U
#define CTRL_TYPE_BIT 4U
#define FRAG_END_BIT 15U
#define PROTOCOL_ACK 1U
#define PROTOCOL_NO_ACK 0U

#define SCB_TRACE(fmt, ...) printk("scbtr|" fmt "\n", ##__VA_ARGS__)

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_le16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static bool bit_get(uint8_t value, uint8_t bit)
{
	return (value & BIT(bit)) != 0U;
}

static void clear_reassembly(struct qcc74xsync_transfer *transfer)
{
	if (transfer->payload != NULL) {
		k_free(transfer->payload);
		transfer->payload = NULL;
	}
	transfer->payload_length = 0U;
	transfer->payload_received = 0U;
}

static int send_packet(struct qcc74xsync_transfer *transfer, uint8_t seq,
			       uint8_t ack, uint8_t event_id, const uint8_t *data,
			       uint16_t length)
{
	uint8_t packet[QCC74XSYNC_PROTOCOL_BUF_SIZE];
	uint16_t total = 0U;
	uint16_t fragment = 0U;
	bool first = true;

	if (transfer->send == NULL || transfer->mtu < 20U ||
	    transfer->mtu > sizeof(packet)) {
		return -EINVAL;
	}

	while (total < length || first) {
		uint8_t ctrl = 0U;
		uint8_t *cursor = &packet[4];
		uint8_t frame_length = 4U;
		uint16_t remaining = transfer->mtu - 4U;
		int payload_size;
		uint8_t *length_field;
		static const uint8_t empty_data;

		if (first) {
			if (remaining < 3U) {
				return -EINVAL;
			}
			write_le16(cursor, length);
			cursor += 2U;
			frame_length += 2U;
			remaining -= 2U;
		}

		length_field = cursor++;
		*length_field = 0U;
		frame_length++;
		remaining--;

		payload_size = qcc74xsync_encrypt_write(
			QCC74XSYNC_PACKET_DATA, event_id, cursor, &frame_length,
			data != NULL ? &data[total] : &empty_data, length - total,
			remaining, first);
		if (payload_size < 0) {
			return payload_size;
		}

		total += (uint16_t)payload_size;
		if (total >= length) {
			packet[2] = (uint8_t)fragment;
			packet[3] = (uint8_t)(fragment >> 8) | 0x80U;
		} else {
			packet[2] = (uint8_t)fragment;
			packet[3] = (uint8_t)(fragment >> 8);
			ctrl |= BIT(CTRL_FRAG_BIT);
		}

		ctrl |= ack ? BIT(CTRL_ACK_BIT) : 0U;
		ctrl |= QCC74XSYNC_PACKET_DATA << CTRL_TYPE_BIT;
		packet[0] = ctrl;
		packet[1] = seq++;
		*length_field = frame_length - (first ? 7U : 5U);
		SCB_TRACE("TX|len=%u|ctrl=%02x|seq=%u|frag=%u|src=%u|payload=%u|total=%u",
			  frame_length, ctrl, packet[1],
			  (uint16_t)packet[2] | ((uint16_t)packet[3] << 8),
			  length, (unsigned int)payload_size, total);

		if (transfer->send(transfer->ctx, packet, frame_length) < 0) {
			return -EIO;
		}

		if (k_sem_take(&transfer->ack_sem,
				       K_MSEC(QCC74XSYNC_PROTOCOL_TIMEOUT_MS)) != 0) {
			SCB_TRACE("TX_ACK|result=timeout|len=%u|seq=%u|frag=%u",
				  frame_length, packet[1],
				  (uint16_t)packet[2] | ((uint16_t)packet[3] << 8));
			return -ETIMEDOUT;
		}

		fragment++;
		first = false;
	}

	return 0;
}

static int send_ack(struct qcc74xsync_transfer *transfer, uint8_t seq,
			    int result)
{
	return send_packet(transfer, seq, PROTOCOL_ACK, (uint8_t)result,
				   transfer->ack_buffer, transfer->ack_length);
}

static int receive_packet(struct qcc74xsync_transfer *transfer,
				  const uint8_t *data, size_t length)
{
	uint8_t ctrl;
	uint8_t seq;
	uint16_t fragment;
	uint16_t fragment_index;
	bool first_fragment;
	const uint8_t *payload;
	size_t payload_length;
	uint8_t event_id = 0U;
	uint8_t event_length = 0U;
	int result;

	if (length < 4U) {
		return -EINVAL;
	}

	ctrl = data[0];
	seq = data[1];
	fragment = read_le16(&data[2]);
	fragment_index = fragment & ~BIT(FRAG_END_BIT);
	first_fragment = fragment_index == 0U;
	payload = &data[4];
	payload_length = length - 4U;
	SCB_TRACE("RX|len=%u|ctrl=%02x|seq=%u|frag=%u|type=%u",
		  (unsigned int)length, ctrl, seq, fragment,
		  (ctrl >> CTRL_TYPE_BIT) & 0x03U);

	if (bit_get(ctrl, CTRL_RETRY_BIT)) {
		if (seq != transfer->old_seq) {
			transfer->old_seq = seq;
			transfer->sequence_started_ms = k_uptime_get();
		}
		if ((k_uptime_get() - transfer->sequence_started_ms) >
		    QCC74XSYNC_PROTOCOL_TIMEOUT_MS) {
			clear_reassembly(transfer);
			return -ETIMEDOUT;
		}
	}

	if (first_fragment) {
		uint8_t source_length;
		uint16_t new_payload_length;
		bool compact_cmd;

		if (payload_length < 3U) {
			return -EINVAL;
		}
		new_payload_length = read_le16(payload);
		source_length = payload[2];
		payload += 3U;
		payload_length -= 3U;
		compact_cmd = ((ctrl >> CTRL_TYPE_BIT) & 0x03U) ==
			QCC74XSYNC_PACKET_CMD && length == 20U &&
			new_payload_length == 4U && source_length == payload_length + 1U &&
			bit_get((uint8_t)(fragment >> 8), 7U);
		if (payload_length != source_length && !compact_cmd) {
			clear_reassembly(transfer);
			return -EINVAL;
		}

		clear_reassembly(transfer);
		transfer->payload_length = new_payload_length;
		if (transfer->payload_length > 0U) {
			transfer->payload = k_malloc(transfer->payload_length);
			if (transfer->payload == NULL) {
				return -ENOMEM;
			}
		}
	} else {
		uint8_t source_length;

		if (payload_length < 1U) {
			return -EINVAL;
		}
		source_length = payload[0];
		payload++;
		payload_length--;
		if (payload_length != source_length) {
			clear_reassembly(transfer);
			return -EINVAL;
		}
	}

	if (transfer->payload_received > transfer->payload_length) {
		return -EINVAL;
	}

	{
		uint8_t empty_payload;
		uint8_t *destination = transfer->payload != NULL ?
			&transfer->payload[transfer->payload_received] : &empty_payload;

		result = qcc74xsync_encrypt_read(
			(ctrl >> CTRL_TYPE_BIT) & 0x03U, &event_id, destination,
			&event_length, payload, payload_length, first_fragment,
			transfer->payload_length - transfer->payload_received);
	}
	if (result < 0) {
		clear_reassembly(transfer);
		return result;
	}

	transfer->payload_received += event_length;
	if (!bit_get(ctrl, CTRL_FRAG_BIT) ||
	    bit_get((uint8_t)(fragment >> 8), 7U)) {
		struct qcc74xsync_event event = {
			.type = ((ctrl >> CTRL_TYPE_BIT) & 0x03U) ==
				QCC74XSYNC_PACKET_CMD ? QCC74XSYNC_EVENT_CMD :
				QCC74XSYNC_EVENT_DATA,
			.id = event_id,
			.data = transfer->payload,
			.length = transfer->payload_received,
		};

		if (transfer->payload_received != transfer->payload_length) {
			clear_reassembly(transfer);
			return -EINVAL;
		}

		transfer->response_length = 0U;
		transfer->ack_buffer = NULL;
		transfer->ack_length = 0U;
		result = transfer->event(transfer->ctx, &event);
		if (result == 0 && transfer->response_length > 0U) {
			transfer->ack_buffer = transfer->response;
			transfer->ack_length = transfer->response_length;
		}
		if (!bit_get(ctrl, CTRL_ACK_BIT)) {
			int ack_result = send_ack(transfer, seq, result);

			transfer->response_length = 0U;
			clear_reassembly(transfer);
			return ack_result < 0 ? ack_result : result;
		}

		transfer->response_length = 0U;
		clear_reassembly(transfer);
		return result;
	}

	return 0;
}

int qcc74xsync_transfer_init(struct qcc74xsync_transfer *transfer,
				     qcc74xsync_send_fn send,
				     qcc74xsync_event_fn event, void *ctx)
{
	if (transfer == NULL || send == NULL || event == NULL) {
		return -EINVAL;
	}

	memset(transfer, 0, sizeof(*transfer));
	transfer->send = send;
	transfer->event = event;
	transfer->ctx = ctx;
	k_sem_init(&transfer->ack_sem, 0, 1);
	return 0;
}

int qcc74xsync_transfer_set_response(struct qcc74xsync_transfer *transfer,
					     const void *data, size_t length)
{
	if (transfer == NULL || data == NULL ||
	    length > sizeof(transfer->response)) {
		return -EINVAL;
	}

	memcpy(transfer->response, data, length);
	transfer->response_length = length;
	return 0;
}

int qcc74xsync_transfer_receive(struct qcc74xsync_transfer *transfer,
					const uint8_t *data, size_t length,
					uint16_t mtu)
{
	if (transfer == NULL || data == NULL || length < 2U || mtu < 20U) {
		return -EINVAL;
	}

	transfer->mtu = mtu;
	return receive_packet(transfer, data, length);
}

int qcc74xsync_transfer_send(struct qcc74xsync_transfer *transfer,
					 const void *data, uint16_t length)
{
	if (transfer == NULL || (length > 0U && data == NULL)) {
		return -EINVAL;
	}

	return send_packet(transfer, 0U, PROTOCOL_NO_ACK, 0U, data, length);
}

int qcc74xsync_transfer_ack(struct qcc74xsync_transfer *transfer)
{
	if (transfer == NULL) {
		return -EINVAL;
	}

	k_sem_give(&transfer->ack_sem);
	return 0;
}

void qcc74xsync_transfer_reset(struct qcc74xsync_transfer *transfer)
{
	if (transfer == NULL) {
		return;
	}

	clear_reassembly(transfer);
	transfer->response_length = 0U;
	transfer->ack_buffer = NULL;
	transfer->ack_length = 0U;
	transfer->sequence_started_ms = 0;
}
