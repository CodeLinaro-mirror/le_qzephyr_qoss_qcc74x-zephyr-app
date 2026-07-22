/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel/thread_stack.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <bshg_zbus_helper.h>
#include <zephyr/tracing/tracing.h>
#include <BSHG_tracing.h>

LOG_MODULE_REGISTER(trigger);

result_t trigger_message_with_reply(const json_msg *request_data, uint32_t timeout_ms,
				    const json_msg *reply)
{
	result_t retval = E_ERROR;
	increase_tracingId();
	BSHG_setTracepoint("trgger_msg_send_app");
	tracing_id_t tracing_id = bshg_zbus_generate_tracing_id();
	bshg_zbus_send_message(BSHG_ZBUS_MEMBER_APP_TRIGGER, BSHG_ZBUS_MEMBER_APP, tracing_id,
			       request_data, timeout_ms);

	// wait for the response
	const struct zbus_channel *zbus_chan = NULL;
	const struct zbus_observer *zbus_sub = NULL;
	struct message_t message;
	zbus_sub = bshg_zbus_subscriber(BSHG_ZBUS_MEMBER_APP_TRIGGER);
	// check for new messages every 100ms, but maximum for timeout_ms
	bool timeout_exceeded = false;
	uint32_t start_time = k_uptime_get_32();
	while (!timeout_exceeded) {
		if (!zbus_sub_wait(zbus_sub, &zbus_chan, K_MSEC(100))) {
			if (!zbus_chan_read(zbus_chan, &message, K_MSEC(100))) {
				if (message.tracing_id == tracing_id) {
					// LOG_INF("Info: Received final reply for tracing_id %d!!",
					// tracing_id);
					if (reply) {
						memcpy((void *)reply, message.data,
						       sizeof(message.data));
					}
					retval = E_SUCCESS;
					BSHG_setTracepoint("triggr_msg_received");
					break;
				}
			}
		}
		if (k_uptime_get_32() - start_time > timeout_ms) {
			timeout_exceeded = true;
			BSHG_setTracepoint("triggr_msg_received");
			break;
		}
	}
	BSHG_stopTracing();
	if (timeout_exceeded) {
		retval = E_TIMEOUT;
		LOG_ERR("Error: TIMEOUT for tracing_id %d!", tracing_id);
	}

	if (retval != E_SUCCESS) {
		LOG_ERR("Error: Some error occured for tracing_id %d!", tracing_id);
	}

	return retval;
}

result_t trigger_message(const json_msg *request_data, uint32_t timeout_ms, const json_msg *reply)
{
	BSHG_startTracing();
	return trigger_message_with_reply(request_data, timeout_ms, reply);
}

void trigger_led_on()
{
	json_msg msg = "{\"ll\":[{\"led\":\"on\"}]}";
	trigger_message(&msg, 1000, NULL);
}
void trigger_led_off()
{
	json_msg msg = "{\"ll\":[{\"led\":\"off\"}]}";
	trigger_message(&msg, 1000, NULL);
}
