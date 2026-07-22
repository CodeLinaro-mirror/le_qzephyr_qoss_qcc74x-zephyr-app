/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>
#include <bshg_spi.h>

#include <jsmn-extended.h>

#include <stdlib.h>

result_t process_msg_ll_spi(const jsmn_json_t *json, json_msg *reply)
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

	uint8_t hex[200] = {};
	int hex_length = 0;
	jsmn_json_t data = {};
	jsmn_json_t delay = {};

	for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (!messages[i].data) {
			if (i == 0) {
				benchi_create_reply_msg(reply, 1, "Error: no messages specified");
			}
			break;
		}

		// mandatory properties
		if (jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"data", 4}},
						 &data)) {
			benchi_create_reply_msg(reply, 1, "Error: missing data");
			return E_ERROR; // Must be specified
		}

		// optional properties
		jsmn_get_object_property_ptr(&messages[i], (jsmn_property_t[]){{"delay_ms", 8}},
					     &delay);

		if (delay.data) {
			k_usleep((int32_t)strtoll(delay.data, NULL, 10));
		}

		if (jsmn_remove_quotes(&data)) {
			benchi_create_reply_msg(reply, 1, "Error: no hexstring");
			return E_ERROR;
		}
		hex_length = (int)hex2bin(data.data, data.length, hex, sizeof(hex));
		if (hex_length == 0) {
			benchi_create_reply_msg(reply, 1, "Error: invalid hexstring");
			return E_ERROR;
		}
		if (BSHG_spiTxRxShortcutFromBinarray(hex, hex_length)) {
			benchi_create_reply_msg(reply, 1, "SPI error");
		}
	}
	benchi_create_reply_msg(reply, 0, "SPI success");
	return E_SUCCESS;
}