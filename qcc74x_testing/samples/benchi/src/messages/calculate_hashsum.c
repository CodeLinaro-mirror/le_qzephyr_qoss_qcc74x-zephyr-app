/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

#include <mbedtls/private/md5.h>
#include <mbedtls/private/sha256.h>

result_t process_msg_calculate_hashsum(const jsmn_json_t *json, json_msg *reply)
{
	result_t retval = E_ERROR;
	jsmn_json_t data = {};
	jsmn_json_t type = {};

	if (jsmn_get_object_property_ptr(json, (jsmn_property_t[]){{"data", 4}}, &data)) {
		benchi_create_reply_msg(reply, 1, "Error: missing data");
		return E_ERROR;
	}
	if (jsmn_get_object_property_ptr(json, (jsmn_property_t[]){{"type", 4}}, &type)) {
		benchi_create_reply_msg(reply, 1, "Error: missing type");
		return E_ERROR;
	}

	uint8_t hash_md5[16] = {0};
	uint8_t hash_sha256[32] = {0};
	char hex_str[65] = {0};
	int result = 1;

	if (!strncmp(type.data, "\"md5\"", 2 + 3)) {
		result = mbedtls_md5(data.data, data.length, hash_md5);
		if (result) {
			benchi_create_reply_msg(reply, 1, "Error while calculating md5");
		} else {
			if (!bin2hex(hash_md5, 16, hex_str, sizeof(hex_str))) {
				benchi_create_reply_msg(
					reply, 1, "Error while converting md5 hashsum to string");
			} else {
				benchi_create_reply_msg(reply, 0, hex_str);
				retval = E_SUCCESS;
			}
		}
	} else if (!strncmp(type.data, "\"sha256\"", 2 + 6)) {
		result = mbedtls_sha256(data.data, data.length, hash_sha256, false);
		if (result) {
			benchi_create_reply_msg(reply, 1, "Error while calculating sha256");
		} else {
			if (!bin2hex(hash_sha256, 32, hex_str, sizeof(hex_str))) {
				benchi_create_reply_msg(
					reply, 1,
					"Error while converting sha256 hashsum to string");
			} else {
				benchi_create_reply_msg(reply, 0, hex_str);
				retval = E_SUCCESS;
			}
		}
	} else {
		benchi_create_reply_msg(reply, 1, "Invalid hashsum requested");
	}
	return retval;
}