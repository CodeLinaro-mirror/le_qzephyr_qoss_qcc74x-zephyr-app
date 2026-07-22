/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bshg_zbus_helper.h"

LOG_MODULE_REGISTER(zbus_helper);

ZBUS_CHAN_DEFINE(sender_chan_to_app, /* Name */
		 struct message_t,   /* Message type */
		 NULL,               /* Validator */
		 NULL,               /* User data */
		 ZBUS_OBSERVERS(     /* observers */
				app_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_CHAN_DEFINE(sender_chan_to_domain, /* Name */
		 struct message_t,      /* Message type */
		 NULL,                  /* Validator */
		 NULL,                  /* User data */
		 ZBUS_OBSERVERS(        /* observers */
				domain_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_CHAN_DEFINE(sender_chan_to_ll, /* Name */
		 struct message_t,  /* Message type */
		 NULL,              /* Validator */
		 NULL,              /* User data */
		 ZBUS_OBSERVERS(    /* observers */
				ll_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_CHAN_DEFINE(reply_chan_to_domain, /* Name */
		 struct message_t,     /* Message type */
		 NULL,                 /* Validator */
		 NULL,                 /* User data */
		 ZBUS_OBSERVERS(       /* observers */
				domain_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_CHAN_DEFINE(reply_chan_to_app, /* Name */
		 struct message_t,  /* Message type */
		 NULL,              /* Validator */
		 NULL,              /* User data */
		 ZBUS_OBSERVERS(    /* observers */
				app_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_CHAN_DEFINE(reply_chan_to_app_trigger, /* Name */
		 struct message_t,          /* Message type */
		 NULL,                      /* Validator */
		 NULL,                      /* User data */
		 ZBUS_OBSERVERS(            /* observers */
				app_trigger_subscriber),
		 ZBUS_MSG_INIT(.msg_id = 0, .data = {}) /* Initial value */
);

ZBUS_SUBSCRIBER_DEFINE(app_subscriber, 6);
ZBUS_SUBSCRIBER_DEFINE(domain_subscriber, 6);
ZBUS_SUBSCRIBER_DEFINE(ll_subscriber, 6);
ZBUS_SUBSCRIBER_DEFINE(app_trigger_subscriber, 6);

/***********************************************************************************************
 * HELPER
 ***********************************************************************************************/

void benchi_create_reply_msg(json_msg *response, int status, const unsigned char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	char message[sizeof(json_msg) - 30] = {}; // make the message buffer a bit shorter
	vsnprintf((char *)message, sizeof(message), msg, args);
	memset(response, 0, sizeof(json_msg));
	snprintf((char *)response, sizeof(json_msg), "{\"status\":%d,\"msg\":\"%s\"}", status,
		 message);
	va_end(args);
}

bshg_zbus_member_t get_member_from_str(const char *member)
{
	if (!strncmp(member, "app_trigger", 11)) {
		return BSHG_ZBUS_MEMBER_APP_TRIGGER;
	}
	if (!strncmp(member, "app", 3)) {
		return BSHG_ZBUS_MEMBER_APP;
	}
	if (!strncmp(member, "domain", 6)) {
		return BSHG_ZBUS_MEMBER_DOMAIN;
	}
	if (!strncmp(member, "ll", 2)) {
		return BSHG_ZBUS_MEMBER_LL;
	}
	if (!strncmp(member, "all", 3)) {
		return BSHG_ZBUS_MEMBER_ALL;
	}
	return BSHG_ZBUS_MEMBER_UNKNOWN;
}

const struct zbus_channel *bshg_zbus_channel(bshg_zbus_channel_t chan, bshg_zbus_member_t receiver)
{
	const struct zbus_channel *retval = NULL;
	switch (chan) {
	case BSHG_ZBUS_CHANNEL_SEND_TO:
		switch (receiver) {
		case BSHG_ZBUS_MEMBER_APP:
			retval = &sender_chan_to_app;
			break;
		case BSHG_ZBUS_MEMBER_DOMAIN:
			retval = &sender_chan_to_domain;
			break;
		case BSHG_ZBUS_MEMBER_LL:
			retval = &sender_chan_to_ll;
			break;
		default:
			retval = NULL;
			break;
		}
		break;
	case BSHG_ZBUS_CHANNEL_REPLY_TO:
		switch (receiver) {
		case BSHG_ZBUS_MEMBER_APP_TRIGGER:
			retval = &reply_chan_to_app_trigger;
			break;
		case BSHG_ZBUS_MEMBER_APP:
			retval = &reply_chan_to_app;
			break;
		case BSHG_ZBUS_MEMBER_DOMAIN:
			retval = &reply_chan_to_domain;
			break;
		default:
			retval = NULL;
			break;
		}
		break;
	}
	if (retval == NULL) {
		LOG_ERR("Error: Channel not found for %d / %d", chan, receiver);
	}
	return retval;
}

const struct zbus_observer *bshg_zbus_subscriber(bshg_zbus_member_t receiver)
{
	switch (receiver) {
	case BSHG_ZBUS_MEMBER_APP:
		return &app_subscriber;
	case BSHG_ZBUS_MEMBER_DOMAIN:
		return &domain_subscriber;
	case BSHG_ZBUS_MEMBER_LL:
		return &ll_subscriber;
	case BSHG_ZBUS_MEMBER_APP_TRIGGER:
		return &app_trigger_subscriber;
	default:
		return NULL;
	}
}

/***********************************************************************************************
 * MESSAGE PREPARATIONS
 ***********************************************************************************************/

uint32_t msg_id_counter = 100; // first 100 msg_ids are reserved
uint32_t bshg_zbus_generate_msg_id()
{
	return msg_id_counter++;
}

uint32_t tracing_id_counter = 1000; // first 100 msg_ids are reserved
uint32_t bshg_zbus_generate_tracing_id()
{
	return tracing_id_counter++;
}

msg_id_t bshg_zbus_prepare_message(struct message_t *msg, bshg_zbus_member_t sender,
				   tracing_id_t tracing_id, const json_msg *data)
{
	msg->msg_id = 0;
	msg->sender = sender;
	msg->tracing_id = tracing_id;
	msg->msg_id = bshg_zbus_generate_msg_id();
	memcpy(msg->data, data, sizeof(msg->data));
	return msg->msg_id;
}

/***********************************************************************************************
 * GENERIC
 ***********************************************************************************************/

msg_id_t bshg_zbus_send(bshg_zbus_member_t sender, bshg_zbus_channel_t chan,
			bshg_zbus_member_t receiver, tracing_id_t tracing_id, const json_msg *data,
			uint32_t timeout_ms)
{
	struct message_t request;
	bshg_zbus_prepare_message(&request, sender, tracing_id, data);

	const struct zbus_channel *zbus_chan = bshg_zbus_channel(chan, receiver);
	// LOG_INF("Info: [%s] (%d -> %d) - msg_id %d - tracing_id %d - %s", zbus_chan->name,
	// request.sender, receiver, request.msg_id, request.tracing_id, request.data);
	zbus_chan_pub(zbus_chan, &request, K_MSEC(timeout_ms));
	return request.msg_id;
}

msg_id_t bshg_zbus_send_message(bshg_zbus_member_t sender, bshg_zbus_member_t receiver,
				tracing_id_t tracing_id, const json_msg *data, uint32_t timeout_ms)
{
	return bshg_zbus_send(sender, BSHG_ZBUS_CHANNEL_SEND_TO, receiver, tracing_id, data,
			      timeout_ms);
}

msg_id_t bshg_zbus_send_reply(bshg_zbus_member_t sender, bshg_zbus_member_t receiver,
			      tracing_id_t tracing_id, const json_msg *data, uint32_t timeout_ms)
{
	return bshg_zbus_send(sender, BSHG_ZBUS_CHANNEL_REPLY_TO, receiver, tracing_id, data,
			      timeout_ms);
}

/***********************************************************************************************
 * SYNC
 ***********************************************************************************************/

result_t bshg_zbus_on_message(bshg_zbus_member_t receiver,
			      void (*callback)(const struct message_t *const msg))
{
	result_t retval = E_ERROR;
	struct message_t message;
	memset(message.data, 0, sizeof(message.data)); // set array to 0

	const struct zbus_channel *zbus_chan = NULL;
	const struct zbus_observer *zbus_sub = bshg_zbus_subscriber(receiver);

	// LOG_INF("Info: Checking for new message on subscriber %s", zbus_sub->name);
	if (!zbus_sub_wait(zbus_sub, &zbus_chan, K_FOREVER)) {
		// LOG_INF("Info: Received message on channel %s", zbus_chan->name);
		if (!zbus_chan_read(zbus_chan, &message, K_MSEC(2))) {
			callback(&message);
		} else {
			LOG_ERR("Error: Error while reading message from channel");
		}
	}
	return retval;
}
