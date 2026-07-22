/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

#include <BSHG_tracing.h>

/********************************************************
 * Domain Messages
 *******************************************************/

result_t domain_process_msg(const jsmn_json_t *message, json_msg *reply)
{
	jsmn_property_t property = {};
	jsmn_json_t json_property = {.data = NULL, .length = 0};

	// ping
	property.data = "ping";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) {
		return process_msg_ping(&json_property, reply);
	}

	// HASHSUM
	property.data = "calculate_hashsum";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_calculate_hashsum(&json_property, reply);
	}

	// ZBUS
	property.data = "zbus";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_zbus(&json_property, reply);
	}

	// WAIT
	property.data = "wait";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_wait(&json_property, reply);
	}

	benchi_create_reply_msg(reply, 1, "Error: Invalid message");
	return E_ERROR;
}

/********************************************************
 * Domain Message Filter ("domain")
 *******************************************************/

void domain_filter_msg(const struct message_t *const message)
{
	jsmn_json_t input = {.data = message->data, .length = strlen(message->data)};
	json_msg reply = {};

	// check for JSON message for this thread --> {"domain":[]}
	const jsmn_property_t property = {.data = "domain", .length = 6};

	BSHG_setTracepoint("domain_start_parse");
	jsmn_json_t domain = {};
	int retcode = jsmn_get_object_property_ptr(&input, &property, &domain);

	// msg. found for domain
	if (retcode == 0) {
		// parse the messages array
		jsmn_json_t messages[30] = {};
		jsmn_get_array_items_ptrs(domain.data, domain.length, messages);

		// iterate over all messages
		BSHG_setTracepoint("domain_process");
		for (int i = 0; i < sizeof(messages); i++) {
			if (!messages[i].data) {
				break;
			}

			if (domain_process_msg(&messages[i], &reply) == E_SUCCESS) {
				BSHG_setTracepoint("domain_send_msg_app");
				bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_DOMAIN, BSHG_ZBUS_MEMBER_APP,
						     message->tracing_id, &reply, 1999);
			}
		}
	}
	// return on parse error
	else if (retcode <= -2) {
		return;
	}

	// forward request from app to ll
	if (message->sender == BSHG_ZBUS_MEMBER_APP) {
		BSHG_setTracepoint("domain_msg_fw_ll");
		bshg_zbus_send_message(BSHG_ZBUS_MEMBER_DOMAIN, BSHG_ZBUS_MEMBER_LL,
				       message->tracing_id, &message->data, 1999);
	}
	// forward request from ll to app
	else if (message->sender == BSHG_ZBUS_MEMBER_LL) {
		BSHG_setTracepoint("domain_msg_fw_app");
		bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_DOMAIN, BSHG_ZBUS_MEMBER_APP,
				     message->tracing_id, &message->data, 1999);
	}
}

/********************************************************
 * Domain Thread
 *******************************************************/

void domain_main(void *p1, void *p2, void *p3)
{
	// reply to a message
	while (true) {
		bshg_zbus_on_message(BSHG_ZBUS_MEMBER_DOMAIN, domain_filter_msg);
	}
}