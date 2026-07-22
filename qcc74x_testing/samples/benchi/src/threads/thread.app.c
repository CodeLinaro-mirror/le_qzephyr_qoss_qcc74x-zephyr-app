/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

#include <BSHG_tracing.h>

/********************************************************
 * App Messages
 *******************************************************/

result_t app_process_msg(const jsmn_json_t *message, json_msg *reply)
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
 * App Message Filter ("app")
 *******************************************************/

void app_filter_msg(const struct message_t *const message)
{
	jsmn_json_t input = {.data = message->data, .length = strlen(message->data)};
	json_msg reply = {};

	// check for JSON message for this thread --> {"app":[]}
	const jsmn_property_t property = {.data = "app", .length = 3};

	BSHG_setTracepoint("app_start_parse");
	jsmn_json_t app = {};
	int retcode = jsmn_get_object_property_ptr(&input, &property, &app);

	// msg. found for app
	if (retcode == 0) {
		// parse the messages array
		jsmn_json_t messages[30] = {};
		jsmn_get_array_items_ptrs(app.data, app.length, messages);

		// iterate over all messages
		BSHG_setTracepoint("app_process");
		for (int i = 0; i < sizeof(messages); i++) {
			if (!messages[i].data) {
				break;
			}

			if (app_process_msg(&messages[i], &reply) == E_SUCCESS) {
				BSHG_setTracepoint("app_send_msg_trggr");
				bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_APP,
						     BSHG_ZBUS_MEMBER_APP_TRIGGER,
						     message->tracing_id, &reply, 1999);
			}
		}
	}
	// return on parse error
	else if (retcode <= -2) {
		return;
	}

	// forward request from trigger to domain
	if (message->sender == BSHG_ZBUS_MEMBER_APP_TRIGGER) {
		BSHG_setTracepoint("app_msg_fw_domain");
		bshg_zbus_send_message(BSHG_ZBUS_MEMBER_APP, BSHG_ZBUS_MEMBER_DOMAIN,
				       message->tracing_id, &message->data,
				       1999); // TODO: modify the request
	}
	// forward request from domain to trigger
	else if (message->sender == BSHG_ZBUS_MEMBER_DOMAIN) {
		BSHG_setTracepoint("app_msg_fw_trigger");
		bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_APP, BSHG_ZBUS_MEMBER_APP_TRIGGER,
				     message->tracing_id, &message->data,
				     1999); // TODO: modify the request
	}
}

/********************************************************
 * App Thread
 *******************************************************/

void app_main(void *p1, void *p2, void *p3)
{
	// reply to a message
	while (true) {
		bshg_zbus_on_message(BSHG_ZBUS_MEMBER_APP, app_filter_msg);
	}
}