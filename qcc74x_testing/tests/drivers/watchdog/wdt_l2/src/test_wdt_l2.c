/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <limits.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define WDT_NODE DT_ALIAS(watchdog0)

#define L2_MAGIC                 0x4C325744U
#define L2_STATE_FIRST           0U
#define L2_STATE_RUNNING         1U
#define L2_STATE_EXPECT_RESET    2U
#define L2_STATE_DONE             3U
#define L2_CYCLES                3U
#define L2_LIFECYCLE_TIMEOUT_MS  200U
#define L2_RESET_TIMEOUT_MS       500U

static volatile uint32_t l2_magic __attribute__((section("WDT_NOINIT")));
static volatile uint32_t l2_state __attribute__((section("WDT_NOINIT")));
static volatile uint32_t l2_cycle __attribute__((section("WDT_NOINIT")));

static const struct device *wdt_device(void)
{
	return DEVICE_DT_GET(WDT_NODE);
}

static void reset_l2_state_if_needed(void)
{
	if (l2_magic != L2_MAGIC) {
		l2_magic = L2_MAGIC;
		l2_state = L2_STATE_FIRST;
		l2_cycle = 0U;
	}
}

static void assert_timeout_boundary(const struct device *wdt)
{
	const struct device *clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(WDT_NODE));
	const clock_control_subsys_t clock_id =
		(clock_control_subsys_t)DT_CLOCKS_CELL(WDT_NODE, id);
	const uint32_t clock_div = DT_PROP(WDT_NODE, clock_div);
	uint32_t base_freq = 0U;
	uint64_t max_ms;
	uint32_t representable_ms;
	uint32_t overrange_ms;
	struct wdt_timeout_cfg cfg = {
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = 0U,
		},
	};
	int err;

	err = clock_control_get_rate(clock_dev, clock_id, &base_freq);
	zassert_equal(err, 0, "clock_control_get_rate failed: %d", err);
	zassert_true(base_freq > 0U, "watchdog source frequency is zero");

	const uint32_t effective_freq = base_freq / (clock_div + 1U);
	zassert_true(effective_freq > 0U, "effective watchdog frequency is zero");

	/* Match the driver contract: floor(UINT16_MAX * 1000 / freq) is valid,
	 * and the next millisecond must produce a value above UINT16_MAX. */
	max_ms = ((uint64_t)UINT16_MAX * 1000U) / effective_freq;
	representable_ms = (max_ms == 0U) ? 1U : (uint32_t)max_ms;
	overrange_ms = representable_ms + 1U;
	TC_PRINT("L2 boundary: base=%u Hz div=%u effective=%u Hz valid=%u ms overrange=%u ms\n",
		 base_freq, clock_div, effective_freq, representable_ms, overrange_ms);

	cfg.window.max = representable_ms;
	err = wdt_install_timeout(wdt, &cfg);
	zassert_equal(err, 0, "representable timeout rejected: %u ms", representable_ms);

	err = wdt_disable(wdt);
	zassert_equal(err, 0, "disable after boundary install failed: %d", err);

	cfg.window.max = overrange_ms;
	err = wdt_install_timeout(wdt, &cfg);
	zassert_equal(err, -EINVAL, "over-range timeout accepted: %u ms", overrange_ms);
}

static void run_repeated_lifecycle(const struct device *wdt)
{
	struct wdt_timeout_cfg cfg = {
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = L2_LIFECYCLE_TIMEOUT_MS,
		},
	};

	for (l2_cycle = 0U; l2_cycle < L2_CYCLES; l2_cycle++) {
		TC_PRINT("L2 lifecycle: cycle %u install/setup/feed/feed/disable\n", l2_cycle);
		int err = wdt_install_timeout(wdt, &cfg);
		zassert_equal(err, 0, "install failed at lifecycle %u: %d", l2_cycle, err);

		err = wdt_setup(wdt, 0U);
		zassert_equal(err, 0, "setup failed at lifecycle %u: %d", l2_cycle, err);

		err = wdt_feed(wdt, 0);
		zassert_equal(err, 0, "initial feed failed at lifecycle %u: %d", l2_cycle, err);
		k_sleep(K_MSEC(20));

		err = wdt_feed(wdt, 0);
		zassert_equal(err, 0, "second feed failed at lifecycle %u: %d", l2_cycle, err);

		err = wdt_disable(wdt);
		zassert_equal(err, 0, "disable failed at lifecycle %u: %d", l2_cycle, err);
	}
}

ZTEST(wdt_l2_test_suite, test_wdt_l2)
{
	const struct device *wdt = wdt_device();
	struct wdt_timeout_cfg cfg = {
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = L2_RESET_TIMEOUT_MS,
		},
	};

	reset_l2_state_if_needed();
	zassert_true(device_is_ready(wdt), "WDT device is not ready");

	if (l2_state == L2_STATE_EXPECT_RESET) {
		TC_PRINT("L2 reset observer: watchdog reset observed, cycle=%u\n", l2_cycle);
		l2_state = L2_STATE_DONE;
		zassert_equal(l2_cycle, L2_CYCLES,
			      "reset occurred before lifecycle completed: %u", l2_cycle);
		return;
	}

	zassert_not_equal(l2_state, L2_STATE_RUNNING,
			  "unexpected watchdog reset during lifecycle cycle %u", l2_cycle);
	zassert_not_equal(l2_state, L2_STATE_DONE, "L2 state was not reset before rerun");

	assert_timeout_boundary(wdt);
	TC_PRINT("L2 boundary: pass\n");

	l2_state = L2_STATE_RUNNING;
	run_repeated_lifecycle(wdt);

	zassert_equal(wdt_install_timeout(wdt, &cfg), 0,
		      "final reset timeout install failed");
	zassert_equal(wdt_setup(wdt, 0U), 0, "final reset timeout setup failed");

	l2_cycle = L2_CYCLES;
	l2_state = L2_STATE_EXPECT_RESET;
	TC_PRINT("L2 intentional expiry: setup complete, waiting for watchdog reset\n");
	k_sleep(K_MSEC(L2_RESET_TIMEOUT_MS * 2U));

	TC_PRINT("L2 reset observer: watchdog reset missing\n");
	zassert_unreachable("watchdog did not reset after final L2 timeout");
}
