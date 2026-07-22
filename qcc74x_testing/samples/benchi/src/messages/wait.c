/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <jsmn-extended.h>

#include <stdlib.h>

result_t process_msg_wait(const jsmn_json_t *json, json_msg *reply)
{
	result_t retval = E_ERROR;
	int32_t wait_time = (int32_t)strtol(json->data, NULL, 10);
	if (wait_time > 0) {
		k_msleep(wait_time);
		benchi_create_reply_msg(reply, 0, "Waited %d milliseconds", wait_time);
		retval = E_SUCCESS;
	} else {
		benchi_create_reply_msg(reply, 1, "Error: invalid value for waiting time");
	}
	return retval;
}