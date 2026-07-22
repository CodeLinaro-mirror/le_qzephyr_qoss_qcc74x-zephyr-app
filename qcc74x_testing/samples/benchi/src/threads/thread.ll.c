/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

#include <BSHG_tracing.h>

/********************************************************
 * Process JSON message key
 *******************************************************/

result_t ll_process_msg(const jsmn_json_t *message, json_msg *reply)
{
	jsmn_property_t property = {};
	jsmn_json_t json_property = {.data = NULL, .length = 0};

	// DOWNLOAD
	property.data = "download";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_ll_download(&json_property, reply);
	}

	// FILE
	property.data = "file";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_ll_file(&json_property, reply);
	}

	// LED
	property.data = "led";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) {
		return process_msg_ll_led(&json_property, reply);
	}

	// SPI
	property.data = "spi";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_ll_spi(&json_property, reply);
	}

	// TRACING
	property.data = "tracing";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_ll_tracing(&json_property, reply);
	}

	// HASHSUM
	property.data = "calculate_hashsum";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_calculate_hashsum(&json_property, reply);
	}

	// PING
	property.data = "ping";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) {
		return process_msg_ping(&json_property, reply);
	}

	// WAIT
	property.data = "wait";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_wait(&json_property, reply);
	}

	// ZBUS
	property.data = "zbus";
	property.length = strlen(property.data);
	jsmn_get_object_property_ptr(message, &property, &json_property);
	if (json_property.data) // it is an according message
	{
		return process_msg_zbus(&json_property, reply);
	}

	// not supported message
	benchi_create_reply_msg(reply, 1, "Error: Invalid message");
	return E_ERROR;
}

/********************************************************
 * Low Level Message Filter ("ll")
 *******************************************************/

void ll_filter_msg(const struct message_t *const message)
{
	jsmn_json_t input = {.data = message->data, .length = strlen(message->data)};
	json_msg reply = {};

	// check for JSON message for this thread --> {"ll":[]}
	const jsmn_property_t property = {.data = "ll", .length = 2};

	BSHG_setTracepoint("ll_start_parse");
	jsmn_json_t ll = {};
	int retcode = jsmn_get_object_property_ptr(&input, &property, &ll);

	// msg. found for ll
	if (retcode == 0) {
		// parse the messages array
		jsmn_json_t messages[30] = {};
		jsmn_get_array_items_ptrs(ll.data, ll.length, messages);

		// iterate over all messages
		BSHG_setTracepoint("ll_process");
		for (int i = 0; i < sizeof(messages); i++) {
			if (!messages[i].data) {
				break;
			}

			if (ll_process_msg(&messages[i], &reply) == E_SUCCESS) {
				BSHG_setTracepoint("ll_send_msg_domain");
				bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_LL, BSHG_ZBUS_MEMBER_DOMAIN,
						     message->tracing_id, &reply, 1999);
			} else {
				benchi_create_reply_msg(
					&reply, 1, "Error: something went wrong with tracing_id %d",
					message->tracing_id);
				bshg_zbus_send_reply(BSHG_ZBUS_MEMBER_LL, BSHG_ZBUS_MEMBER_DOMAIN,
						     message->tracing_id, &reply, 1999);
			}
		}
	}
}

/********************************************************
 * Low Level Thread
 *******************************************************/

void ll_main(void *p1, void *p2, void *p3)
{
	// reply to a message
	while (true) {
		bshg_zbus_on_message(BSHG_ZBUS_MEMBER_LL, ll_filter_msg);
	}
}
