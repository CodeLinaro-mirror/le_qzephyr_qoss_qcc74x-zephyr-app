/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/zbus/zbus.h>

#ifndef ZBUS_HELPER_H
#define ZBUS_HELPER_H

#include <bshg/types.h>

typedef uint32_t msg_id_t;
typedef uint32_t tracing_id_t;

typedef enum {
	BSHG_ZBUS_MEMBER_UNKNOWN = 0,
	BSHG_ZBUS_MEMBER_ALL = 0,
	BSHG_ZBUS_MEMBER_APP_TRIGGER,
	BSHG_ZBUS_MEMBER_APP,
	BSHG_ZBUS_MEMBER_DOMAIN,
	BSHG_ZBUS_MEMBER_LL
} bshg_zbus_member_t;

typedef enum {
	BSHG_ZBUS_CHANNEL_SEND_TO = 0,
	BSHG_ZBUS_CHANNEL_REPLY_TO
} bshg_zbus_channel_t;

struct message_t {
	bshg_zbus_member_t sender;
	msg_id_t msg_id;
	tracing_id_t tracing_id;
	json_msg data;
};

const struct zbus_observer *bshg_zbus_subscriber(bshg_zbus_member_t receiver);

bshg_zbus_member_t get_member_from_str(const char *member);

uint32_t bshg_zbus_generate_tracing_id();
msg_id_t bshg_zbus_prepare_message(struct message_t *msg, bshg_zbus_member_t sender,
				   tracing_id_t tracing_id, const json_msg *data);

msg_id_t bshg_zbus_send_message(bshg_zbus_member_t sender, bshg_zbus_member_t receiver,
				tracing_id_t tracing_id, const json_msg *data, uint32_t timeout_ms);
msg_id_t bshg_zbus_send_reply(bshg_zbus_member_t sender, bshg_zbus_member_t receiver,
			      tracing_id_t tracing_id, const json_msg *data, uint32_t timeout_ms);

result_t bshg_zbus_on_message(bshg_zbus_member_t receiver,
			      void (*callback)(const struct message_t *const msg));

void benchi_create_reply_msg(json_msg *response, int status, const unsigned char *msg, ...);

#endif // ZBUS_HELPER_H