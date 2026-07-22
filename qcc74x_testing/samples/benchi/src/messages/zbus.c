/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

result_t process_msg_zbus(const jsmn_json_t *json, json_msg *reply)
{
	if (json->data[0] != '[') {
		return E_ERROR;
	}
	if (json->data[json->length - 1] != ']') {
		return E_ERROR;
	}

	jsmn_json_t messages[30] = {};
	jsmn_get_array_items_ptrs(json->data, json->length, messages);

	jsmn_json_t channel = {};
	jsmn_json_t to = {};
	jsmn_json_t data = {};
	jsmn_json_t blocking = {};

	for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (!messages[i].data) {
			break;
		}

		// mandatory properties
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"channel", 7}},
						 &channel)) {
			benchi_create_reply_msg(reply, 1, "Error: missing channel");
			return E_ERROR; // Must be specified
		}
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"data", 4}},
						 &data)) {
			benchi_create_reply_msg(reply, 1, "Error: missing data");
			return E_ERROR; // Must be specified
		}
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"to", 2}},
						 &to)) {
			benchi_create_reply_msg(reply, 1, "Error: missing receiver");
			return E_ERROR; // Must be specified
		}

		// optional properties
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"blocking", 8}},
					     &blocking);

		jsmn_remove_quotes(&to);
		bshg_zbus_member_t sender = get_member_from_str(k_current_get()->name);
		bshg_zbus_member_t receiver = get_member_from_str(to.data);
		tracing_id_t tracing_id = bshg_zbus_generate_tracing_id();

		jsmn_remove_quotes(&channel);
		if (!strncmp(channel.data, "send", 4)) {
			bshg_zbus_send_message(sender, receiver, tracing_id, (json_msg *)data.data,
					       100);
		} else if (!strncmp(channel.data, "reply", 5)) {
			bshg_zbus_send_reply(sender, receiver, tracing_id, (json_msg *)data.data,
					     100);
		}
	}

	benchi_create_reply_msg(reply, 0, "Successfully sent zbus message");
	return E_SUCCESS;
}