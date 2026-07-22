/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>
#include <bshg_download.h>

#include <BSHG_tracing.h>

result_t process_msg_ll_download(const jsmn_json_t *json, json_msg *reply)
{
	if (json->data[0] != '[') {
		benchi_create_reply_msg(reply, 0, "Error: must be an array");
		return E_ERROR;
	}
	if (json->data[json->length - 1] != ']') {
		benchi_create_reply_msg(reply, 0, "Error: must be an array");
		return E_ERROR;
	}

	jsmn_json_t messages[30] = {};
	jsmn_get_array_items_ptrs(json->data, json->length, messages);

	jsmn_json_t url = {};
	jsmn_json_t dst = {};

	for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (!messages[i].data) {
			if (i == 0) {
				benchi_create_reply_msg(reply, 1, "Error: no messages specified");
			}
			break;
		}

		// mandatory properties
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"url", 3}},
						 &url)) {
			benchi_create_reply_msg(reply, 1, "Error: missing url");
			return E_ERROR; // Must be specified
		}

		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"dst", 3}},
						 &dst)) {
			benchi_create_reply_msg(reply, 1, "Error: missing dst");
			return E_ERROR; // Must be specified
		}

		if (jsmn_remove_quotes(&url)) {
			benchi_create_reply_msg(reply, 1, "Error: no url string");
			return E_ERROR;
		}
		if (jsmn_remove_quotes(&dst)) {
			benchi_create_reply_msg(reply, 1, "Error: no dst string");
			return E_ERROR;
		}

		BSHG_setTracepoint_extended("download_start");
		if (bshg_start_download(&url, &dst) < 0) {
			BSHG_setTracepoint_extended("download_failed");
			benchi_create_reply_msg(reply, 1, "Error: download failed");
			return E_ERROR;
		} else {
			BSHG_setTracepoint_extended("download_success");
		}
	}
	benchi_create_reply_msg(reply, 0, "Download success");
	return E_SUCCESS;
}