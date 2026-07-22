/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <bshg/types.h>
#include <bshg_zbus_helper.h>

#include <jsmn-extended.h>

// LL specific messages
result_t process_msg_ll_led(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_ll_spi(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_ll_file(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_ll_tracing(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_ll_download(const jsmn_json_t *json, json_msg *reply);

// generic messages
result_t process_msg_calculate_hashsum(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_ping(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_zbus(const jsmn_json_t *json, json_msg *reply);
result_t process_msg_wait(const jsmn_json_t *json, json_msg *reply);
