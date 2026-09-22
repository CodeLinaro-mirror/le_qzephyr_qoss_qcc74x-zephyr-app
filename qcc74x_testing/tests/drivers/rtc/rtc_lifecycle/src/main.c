/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <time.h>

#define RTC_LIFECYCLE_ITERATIONS 8
#define RTC_LIFECYCLE_TOLERANCE_SECONDS 1

static const struct device *const rtc = DEVICE_DT_GET(DT_ALIAS(rtc));

static const time_t lifecycle_times[RTC_LIFECYCLE_ITERATIONS] = {
	1767225595, /* 2025-12-31 23:59:55 UTC */
	1767229195, /* 2026-01-01 00:59:55 UTC */
	1767232795, /* 2026-01-01 01:59:55 UTC */
	1767236395, /* 2026-01-01 02:59:55 UTC */
	1767239995, /* 2026-01-01 03:59:55 UTC */
	1767243595, /* 2026-01-01 04:59:55 UTC */
	1767247195, /* 2026-01-01 05:59:55 UTC */
	1767250795, /* 2026-01-01 06:59:55 UTC */
};

ZTEST(rtc_lifecycle, test_repeated_set_get)
{
	zassert_true(device_is_ready(rtc), "RTC device is not ready");

	for (size_t i = 0; i < ARRAY_SIZE(lifecycle_times); i++) {
		struct rtc_time expected;
		struct rtc_time actual;
		time_t expected_seconds;
		time_t actual_seconds;

		zassert_not_null(gmtime_r(&lifecycle_times[i], (struct tm *)&expected),
				"Failed to convert lifecycle timestamp %d", i);
		expected.tm_isdst = -1;
		expected.tm_nsec = 0;
		expected_seconds = timeutil_timegm((struct tm *)&expected);

		zassert_ok(rtc_set_time(rtc, &expected), "Failed to set lifecycle value %d", i);
		zassert_ok(rtc_get_time(rtc, &actual), "Failed to get lifecycle value %d", i);

		actual_seconds = timeutil_timegm((struct tm *)&actual);
		zassert_true(actual_seconds >= expected_seconds &&
				     actual_seconds <= expected_seconds + RTC_LIFECYCLE_TOLERANCE_SECONDS,
				     "Lifecycle value %d was not replaced: expected %lld, got %lld", i,
				     (long long)expected_seconds, (long long)actual_seconds);
	}
}

ZTEST_SUITE(rtc_lifecycle, NULL, NULL, NULL, NULL, NULL);
