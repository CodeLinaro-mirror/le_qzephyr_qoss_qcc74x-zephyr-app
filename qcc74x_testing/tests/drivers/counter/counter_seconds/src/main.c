/*
 * Copyright (c) 2019 Intel Corp.
 *
 * SPDX-License-Identifier: Apache-2.0.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 * Not a contribution.
 */
#include <zephyr/drivers/counter.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

/*
 * Basic test to ensure that the clock is ticking at roughly 10Hz.
 *
 * qcc74x_testing-owned copy of zephyr/tests/drivers/counter/counter_seconds,
 * ported per the CONFIG_COUNTER_TMR_QCC74X vendor adaptation confirmed in
 * the reference qcc74x_zephyr_unikie tree's "Apply patches from
 * qcc74xzephyrporting" commit: the qcc74x timer0's slowest available clock
 * source (QC7XX_TIMER_CLK_1K, 1kHz) with the maximum 8-bit clock divider
 * (99, giving a divisor of 100) yields 10Hz, not the vanilla test's assumed
 * 1Hz -- so the device selection and bounds are adjusted to match, not the
 * DELAY_MS/test structure itself.
 */
#ifdef CONFIG_COUNTER_TMR_QCC74X
#define CTR_DEV DT_NODELABEL(timer0)
#else
#ifdef CONFIG_COUNTER_CMOS
#define CTR_DEV		DT_COMPAT_GET_ANY_STATUS_OKAY(motorola_mc146818)
#else
#define CTR_DEV		DT_ALIAS(rtc_0)
#endif
#endif

#define DELAY_MS 1200	/* pause 1.2 seconds should always pass */
#ifdef CONFIG_COUNTER_TMR_QCC74X
#define MIN_BOUND 10	/* counter must report at least MIN_BOUND .. */
#define MAX_BOUND 20	/* .. but at most MAX_BOUND seconds elapsed */
#else
#define MIN_BOUND 1	/* counter must report at least MIN_BOUND .. */
#define MAX_BOUND 2	/* .. but at most MAX_BOUND seconds elapsed */
#endif

ZTEST(seconds_counter, test_seconds_rate)
{
	const struct device *const dev = DEVICE_DT_GET(CTR_DEV);
	uint32_t start, elapsed;
	int err;

	zassert_true(device_is_ready(dev), "counter device is not ready");

	err = counter_get_value(dev, &start);
	zassert_true(err == 0, "failed to read counter device");

	k_msleep(DELAY_MS);

	err = counter_get_value(dev, &elapsed);
	zassert_true(err == 0, "failed to read counter device");
	elapsed -= start;

	zassert_true(elapsed >= MIN_BOUND, "busted minimum bound");
	zassert_true(elapsed <= MAX_BOUND, "busted maximum bound");
}

ZTEST_SUITE(seconds_counter, NULL, NULL, NULL, NULL, NULL);
