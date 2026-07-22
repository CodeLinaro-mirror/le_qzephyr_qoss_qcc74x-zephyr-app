/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

result_t process_msg_ping(const jsmn_json_t *json, json_msg *reply)
{
	result_t retval = E_ERROR;
	if (!strncmp(json->data, "true", 4)) {
		benchi_create_reply_msg(reply, 0, "pong");
		retval = E_SUCCESS;
	} else {
		benchi_create_reply_msg(reply, 1, "Error: invalid value");
	}
	return retval;
}