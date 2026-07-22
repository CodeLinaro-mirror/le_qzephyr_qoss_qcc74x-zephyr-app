/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/ztest.h>

#include <bshg/filesystem.h>
#include <bshg_zbus_helper.h>
#include <jsmn-extended.h>

// process_msg function
extern result_t ll_process_msg(const jsmn_json_t *message, json_msg *reply);

result_t test_ll_msg_is_supported(char *json)
{
	result_t retval = E_ERROR;
	json_msg reply;
	jsmn_json_t msg = {.data = json};
	msg.length = strlen(msg.data);

	// print the reply
	printf("Send: %s\n", msg.data);
	retval = ll_process_msg(&msg, &reply);
	printf("Reply: %s\n", reply);
	return retval;
}

ZTEST(thread_ll_tests, test_messages_supported)
{
	// LED
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"led\":\"on\"}"));
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"led\":\"off\"}"));

	// SPI
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"spi\":[{\"data\":\"0123456789ABCDEF\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"spi\":[{\"data\":\"0123456789ABCDEF\",\"delay_ms\":2}]}"));

	// PING
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"ping\":true}"));

	// FILE
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"write\",\"path\":\"/lfs/"
					       "data\",\"data\":\"anydata\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"generate\",\"path\":\"/"
					       "lfs/small\",\"size\":10}]}"));
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"file\":[{\"action\":\"generate\","
							  "\"path\":\"/lfs/big\",\"size\":500}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"generate\",\"path\":\"/"
					       "lfs/verybig\",\"size\":1000000}]}"));
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"file\":[{\"action\":\"raw_write\","
							  "\"offset\":0,\"data\":\"anydata\"}]}"));
	zassert_equal(
		E_SUCCESS,
		test_ll_msg_is_supported(
			"{\"file\":[{\"action\":\"raw_generate\",\"offset\":0,\"size\":100}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"raw_erase\",\"offset\":0,\"size\":100}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"raw_erase_full\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"raw_clean_generate\","
					       "\"offset\":0,\"size\":100}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"raw_clean_write\","
					       "\"offset\":0,\"data\":\"anydata\"}]}"));
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"file\":[{\"action\":\"raw_verify\","
							  "\"offset\":0,\"data\":\"anydata\"}]}"));

	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"read\",\"path\":\"/lfs/data\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"read\",\"path\":\"/lfs/small\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"read\",\"path\":\"/lfs/big\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"read\",\"path\":\"/lfs/verybig\"}]}"));

	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"remove\",\"path\":\"/lfs/data\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"remove\",\"path\":\"/lfs/small\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"remove\",\"path\":\"/lfs/big\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"file\":[{\"action\":\"remove\",\"path\":\"/lfs/verybig\"}]}"));

	// HASHSUM
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported(
			      "{\"calculate_hashsum\":{\"type\":\"md5\",\"data\":\"anydata\"}}"));
	zassert_equal(
		E_SUCCESS,
		test_ll_msg_is_supported(
			"{\"calculate_hashsum\":{\"type\":\"sha256\",\"data\":\"anydata\"}}"));

	// TRACING
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"tracing\":{\"init\":true}}"));

	// ZBUS
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":"
							  "\"app\",\"data\":{\"ping\":true}}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":\"domain\","
					       "\"data\":{\"ping\":true}}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":\"app\","
					       "\"data\":{\"ping\":true},\"blocking\":false}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"zbus\":[{\"channel\":\"send\",\"to\":\"domain\","
					       "\"data\":{\"ping\":true},\"blocking\":false}]}"));

	// DOWNLOAD
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"download\":[{\"url\":\"https://postman-echo.com/"
					       "bytes/1\",\"dst\":\"littlefs\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"download\":[{\"url\":\"https://postman-echo.com/"
					       "bytes/1\",\"dst\":\"flash\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"download\":[{\"url\":\"https://postman-echo.com/"
					       "bytes/1\",\"dst\":\"ram\"}]}"));
	zassert_equal(E_SUCCESS,
		      test_ll_msg_is_supported("{\"download\":[{\"url\":\"https://postman-echo.com/"
					       "bytes/1\",\"dst\":\"discard\"}]}"));

	// WAIT
	zassert_equal(E_SUCCESS, test_ll_msg_is_supported("{\"wait\":10}"));
}

ZTEST(thread_ll_tests, test_messages_unsupported)
{
	// LED
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"led\":123}"));         // not a string
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"led\":\"invalid\"}")); // invalid value

	// SPI
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"spi\":123}")); // not an array
	zassert_equal(E_ERROR,
		      test_ll_msg_is_supported("{\"spi\":[{\"data\":123}]}")); // not a string
	zassert_equal(E_ERROR,
		      test_ll_msg_is_supported(
			      "{\"spi\":[{\"data\":\"notahexstring\"}]}")); // not a hexstring

	// PING
	zassert_equal(E_ERROR,
		      test_ll_msg_is_supported("{\"ping\":123}")); // invalid value for ping

	// FILE
	zassert_equal(E_ERROR, test_ll_msg_is_supported(
				       "{\"file\":[{\"action\":\"invalid\"}]}")); // invalid action
	zassert_equal(E_ERROR,
		      test_ll_msg_is_supported("{\"file\":[{\"action\":\"read\"}]}")); // missing
	zassert_equal(E_ERROR, test_ll_msg_is_supported(
				       "{\"file\":[{\"action\":\"generate\",\"path\":\"/lfs/"
				       "bigfile\",\"size\":\"123\"}]}")); // size is string

	// TRACING
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"tracing\":{\"init\":false}}"));
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"tracing\":{\"init\":1234}}"));

	// HASHSUM
	zassert_equal(
		E_ERROR,
		test_ll_msg_is_supported(
			"{\"calculate_hashsum\":{\"type\":\"xxx\",\"data\":\"anydata\"}}")); // invalid
											     // type
	zassert_equal(E_ERROR,
		      test_ll_msg_is_supported(
			      "{\"calculate_hashsum\":{\"type\":\"sha256\"}}")); // missing data

	// DOWNLOAD
	zassert_equal(E_ERROR, test_ll_msg_is_supported(
				       "{\"download\":[{\"url\":\"123\"}]}")); // missing dst
	zassert_equal(
		E_ERROR,
		test_ll_msg_is_supported(
			"{\"download\":[{\"url\":123,\"dst\":\"littlefs\"}]}")); // invalid url
	zassert_equal(
		E_ERROR,
		test_ll_msg_is_supported(
			"{\"download\":[{\"url\":\"123\",\"dst\":\"invalid\"}]}")); // invalid dst

	// WAIT
	zassert_equal(E_ERROR, test_ll_msg_is_supported("{\"wait\":\"10\"}")); // invalid type
}

void *thread_ll_tests_setup(void)
{
	mount_littlefs();
	return NULL;
}

ZTEST_SUITE(thread_ll_tests, NULL, thread_ll_tests_setup, NULL, NULL, NULL);