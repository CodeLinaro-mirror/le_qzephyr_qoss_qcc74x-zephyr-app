/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <BSHG_tracing.h>

result_t process_msg_ll_tracing(const jsmn_json_t *json, json_msg *reply)
{
	result_t retval = E_ERROR;
	jsmn_json_t init = {};
	jsmn_get_object_property_ptr((jsmn_json_t *)&json->data, (jsmn_property_t[]){{"init", 4}},
				     &init);
	if (!strncmp(init.data, "true", 4)) {
		BSHG_startTracing();
		benchi_create_reply_msg(reply, 0, "Initialized RAM Tracing");
		retval = E_SUCCESS;
	} else {
		benchi_create_reply_msg(reply, 1, "Did not initialize RAM Tracing");
	}
	return retval;
}