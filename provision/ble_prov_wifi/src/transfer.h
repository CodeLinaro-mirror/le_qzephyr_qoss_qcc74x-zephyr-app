/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74XSYNC_TRANSFER_H_
#define QCC74XSYNC_TRANSFER_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "pro_config.h"

#define QCC74XSYNC_PROTOCOL_VERSION "v1.1.0"

#define QCC74XSYNC_PROTOCOL_RESULT_OK 0U
#define QCC74XSYNC_PROTOCOL_RESULT_NOT_READY 1U
#define QCC74XSYNC_PROTOCOL_RESULT_ERROR 2U
#define QCC74XSYNC_PROTOCOL_RESULT_NO_MEM 3U
#define QCC74XSYNC_PROTOCOL_RESULT_TIMEOUT 4U

enum qcc74xsync_event_type {
	QCC74XSYNC_EVENT_CMD = 0,
	QCC74XSYNC_EVENT_DATA = 1,
};

enum qcc74xsync_cmd_id {
	QCC74XSYNC_CMD_ACK = 0x00,
	QCC74XSYNC_CMD_SET_SEC_MODE,
	QCC74XSYNC_CMD_SET_OP_MODE,
	QCC74XSYNC_CMD_CONNECT_WIFI,
	QCC74XSYNC_CMD_DISCONNECT_WIFI,
	QCC74XSYNC_CMD_GET_WIFI_STATUS,
	QCC74XSYNC_CMD_DEAUTHENTICATE,
	QCC74XSYNC_CMD_GET_VERSION,
	QCC74XSYNC_CMD_CLOSE_CONNECTION,
	QCC74XSYNC_CMD_WIFI_SCAN,
	QCC74XSYNC_CMD_DATA_GET_PREPARE,
	QCC74XSYNC_CMD_WIFI_DATA_GET,
	QCC74XSYNC_CMD_PROV_STOP,
	QCC74XSYNC_CMD_WIFI_STATE_GET,
};

enum qcc74xsync_data_id {
	QCC74XSYNC_DATA_ACK = 0x00,
	QCC74XSYNC_DATA_STA_WIFI_BSSID,
	QCC74XSYNC_DATA_STA_WIFI_SSID,
	QCC74XSYNC_DATA_STA_WIFI_PASSWORD,
	QCC74XSYNC_DATA_SOFTAP_WIFI_SSID,
	QCC74XSYNC_DATA_SOFTAP_WIFI_PASSWORD,
	QCC74XSYNC_DATA_SOFTAP_MAX_CONNECTION_COUNT,
	QCC74XSYNC_DATA_SOFTAP_AUTH_MODE,
	QCC74XSYNC_DATA_SOFTAP_CHANNEL,
	QCC74XSYNC_DATA_USERNAME,
	QCC74XSYNC_DATA_CA_CERTIFICATION,
	QCC74XSYNC_DATA_CLIENT_CERTIFICATION,
	QCC74XSYNC_DATA_SERVER_CERTIFICATION,
	QCC74XSYNC_DATA_CLIENT_PRIVATE_KEY,
	QCC74XSYNC_DATA_SERVER_PRIVATE_KEY,
	QCC74XSYNC_DATA_WIFI_CONNECTION_STATE,
	QCC74XSYNC_DATA_VERSION,
	QCC74XSYNC_DATA_WIFI_LIST,
	QCC74XSYNC_DATA_ERROR,
	QCC74XSYNC_DATA_CUSTOM_DATA,
};

struct qcc74xsync_event {
	enum qcc74xsync_event_type type;
	uint8_t id;
	const uint8_t *data;
	size_t length;
};

typedef int (*qcc74xsync_send_fn)(void *ctx, const uint8_t *data,
					 size_t length);
typedef int (*qcc74xsync_event_fn)(void *ctx,
					 const struct qcc74xsync_event *event);

struct qcc74xsync_transfer {
	qcc74xsync_send_fn send;
	qcc74xsync_event_fn event;
	void *ctx;

	uint8_t old_seq;
	uint8_t *payload;
	uint16_t payload_length;
	uint16_t payload_received;
	uint16_t mtu;

	uint8_t response[QCC74XSYNC_PROTOCOL_BUF_SIZE];
	uint16_t response_length;
	uint8_t *ack_buffer;
	uint16_t ack_length;

	int64_t sequence_started_ms;
	struct k_sem ack_sem;
};

int qcc74xsync_transfer_init(struct qcc74xsync_transfer *transfer,
				     qcc74xsync_send_fn send,
				     qcc74xsync_event_fn event, void *ctx);
int qcc74xsync_transfer_receive(struct qcc74xsync_transfer *transfer,
					const uint8_t *data, size_t length,
					uint16_t mtu);
int qcc74xsync_transfer_send(struct qcc74xsync_transfer *transfer,
					 const void *data, uint16_t length);
int qcc74xsync_transfer_ack(struct qcc74xsync_transfer *transfer);
int qcc74xsync_transfer_set_response(struct qcc74xsync_transfer *transfer,
					     const void *data, size_t length);
void qcc74xsync_transfer_reset(struct qcc74xsync_transfer *transfer);

#endif /* QCC74XSYNC_TRANSFER_H_ */
