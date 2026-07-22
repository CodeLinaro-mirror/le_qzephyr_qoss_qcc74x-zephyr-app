/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/messages.h>

#include <zephyr/drivers/led.h>

result_t process_msg_ll_led(const jsmn_json_t *json, json_msg *reply)
{
	result_t retval = E_ERROR;
	static const struct device *leds_device = DEVICE_DT_GET_ANY(gpio_leds);
	if (!strncmp(json->data, "\"on\"", 2 + 2)) {
		led_on(leds_device, 0);
		benchi_create_reply_msg(reply, 1, "LED turned on");
		retval = E_SUCCESS;
	} else if (!strncmp(json->data, "\"off\"", 2 + 3)) {
		led_off(leds_device, 0);
		benchi_create_reply_msg(reply, 0, "LED turned off");
		retval = E_SUCCESS;
	} else {
		benchi_create_reply_msg(reply, 1, "LED invalid value");
	}
	return retval;
}