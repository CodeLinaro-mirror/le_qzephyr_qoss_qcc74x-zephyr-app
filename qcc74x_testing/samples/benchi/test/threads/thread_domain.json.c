/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/ztest.h>

#include <bshg/filesystem.h>
#include <bshg_zbus_helper.h>
#include <jsmn-extended.h>

// process_msg function
extern result_t domain_process_msg(const jsmn_json_t *message, json_msg *reply);

result_t test_domain_msg_is_supported(char *json)
{
	result_t retval = E_ERROR;
	json_msg reply;
	jsmn_json_t msg = {.data = json};
	msg.length = strlen(msg.data);

	// print the reply
	retval = domain_process_msg(&msg, &reply);
	printf("Reply: %s\n", reply);
	return retval;
}

ZTEST(thread_domain_tests, test_messages_supported)
{
	// PING
	zassert_equal(E_SUCCESS, test_domain_msg_is_supported("{\"ping\":true}"));

	// HASHSUM
	zassert_equal(E_SUCCESS,
		      test_domain_msg_is_supported(
			      "{\"calculate_hashsum\":{\"type\":\"md5\",\"data\":\"anydata\"}}"));
	zassert_equal(
		E_SUCCESS,
		test_domain_msg_is_supported(
			"{\"calculate_hashsum\":{\"type\":\"sha256\",\"data\":\"anydata\"}}"));

	// ZBUS
	zassert_equal(E_SUCCESS,
		      test_domain_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":"
						   "\"app\",\"data\":{\"ping\":true}}]}"));
	zassert_equal(E_SUCCESS,
		      test_domain_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":\"ll\","
						   "\"data\":{\"ping\":true}}]}"));
	zassert_equal(E_SUCCESS, test_domain_msg_is_supported(
					 "{\"zbus\":[{\"channel\":\"send\",\"to\":\"app\",\"data\":"
					 "{\"ping\":true},\"blocking\":false}]}"));
	zassert_equal(E_SUCCESS, test_domain_msg_is_supported(
					 "{\"zbus\":[{\"channel\":\"send\",\"to\":\"ll\",\"data\":{"
					 "\"ping\":true},\"blocking\":false}]}"));

	// WAIT
	zassert_equal(E_SUCCESS, test_domain_msg_is_supported("{\"wait\":10}"));
}

ZTEST(thread_domain_tests, test_messages_unsupported)
{
	// PING
	zassert_equal(E_ERROR,
		      test_domain_msg_is_supported("{\"ping\":123}")); // invalid value for ping

	// HASHSUM
	zassert_equal(
		E_ERROR,
		test_domain_msg_is_supported(
			"{\"calculate_hashsum\":{\"type\":\"xxx\",\"data\":\"anydata\"}}")); // invalid
											     // type
	zassert_equal(E_ERROR,
		      test_domain_msg_is_supported(
			      "{\"calculate_hashsum\":{\"type\":\"sha256\"}}")); // missing data

	// WAIT
	zassert_equal(E_ERROR, test_domain_msg_is_supported("{\"wait\":\"10\"}")); // invalid type
}

void *thread_domain_tests_setup(void)
{
	mount_littlefs();
	return NULL;
}

ZTEST_SUITE(thread_domain_tests, NULL, thread_domain_tests_setup, NULL, NULL, NULL);