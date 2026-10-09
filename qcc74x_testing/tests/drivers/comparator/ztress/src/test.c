/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Comparator Ztress stress test for QCC74x.
 *
 * Context model (see COMPARATOR_GPIO_LOOPBACK_DESIGN.md):
 *   ZTRESS_TIMER  - ISR context request/epoch producer. Must not call any
 *                   GPIO/comparator API, block, sleep or allocate.
 *   ZTRESS_THREAD - acomp_owner: the SOLE owner of GPIO20 and the comparator
 *                   API. Performs one controlled transition per invocation and
 *                   validates it against the current phase mode.
 *   ZTRESS_THREAD - cpu_noise: scheduler contention only; never touches GPIO
 *                   or the comparator.
 *
 * Owner thread execution count drives completion (deterministic). Because a
 * comparator edge can coalesce in HBN hardware if not consumed before the next
 * transition, the owner always fully consumes the callback/pending state of the
 * previous edge before starting the next.
 */

#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static const struct device *TEST_DEV = DEVICE_DT_GET(DT_ALIAS(test_comp));
static const struct gpio_dt_spec TEST_PIN = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), test_gpios);

/* Compromise value: slow bias (power mode 1) needs a few hundred us to settle.
 * Start high enough to be reliable; tighten after hardware characterization.
 */
#define SETTLE_US 1000
#define CB_WAIT_MS 100

/* Phase modes */
enum phase_mode {
	MODE_RISING,
	MODE_FALLING,
	MODE_BOTH,
	MODE_PENDING,
	MODE_REARM,
};

/* Shared control state between ISR callback, timer and owner thread. */
static struct {
	atomic_t cb_hits;    /* incremented by ISR callback */
	atomic_t transitions; /* validated edges performed by owner */
	atomic_t fails;       /* validated failures encountered by owner */
	atomic_t epoch;       /* monotonically increasing timer token */
	atomic_t requested;   /* timer request token (informational) */
} st;

static K_SEM_DEFINE(cb_sem, 0, 1);
static enum phase_mode g_mode;

/* ------------------------------------------------------------------ */
/*  ISR-safe comparator callback                                        */
/*  No GPIO/comparator calls, no blocking, no allocation, no zassert.   */
/* ------------------------------------------------------------------ */
static void acomp_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	atomic_inc(&st.cb_hits);
	k_sem_give(&cb_sem);
}

/* ------------------------------------------------------------------ */
/*  Owner helpers                                                       */
/* ------------------------------------------------------------------ */
static void flush_cb(void)
{
	while (k_sem_take(&cb_sem, K_NO_WAIT) == 0) {
	}
}

static void drive(uint32_t level)
{
	gpio_pin_set_dt(&TEST_PIN, level);
	k_usleep(SETTLE_US);
}

/* Verify that exactly 'expect' callbacks arrived since baseline 'before'.
 * 'before' MUST be captured before driving the edge that fires the callback,
 * otherwise the already-incremented count is mistaken for a pre-existing one.
 */
static bool check_cb_delta(uint32_t before, bool expect)
{
	if (expect) {
		if (k_sem_take(&cb_sem, K_MSEC(CB_WAIT_MS)) != 0) {
			return false;
		}
		k_usleep(SETTLE_US);
	} else {
		/* Let any spurious callback for the opposite edge surface. */
		k_usleep(SETTLE_US);
	}

	uint32_t after = atomic_get(&st.cb_hits);
	bool match = (after - before) == (expect ? 1u : 0u);

	return match;
}

/* Pending read-and-clear semantics: returns true only if it reads 1 then 0. */
static bool pending_read_clear(bool expect)
{
	int pending;

	pending = comparator_trigger_is_pending(TEST_DEV);
	if (expect) {
		if (pending != 1) {
			return false;
		}
		k_usleep(SETTLE_US);
		if (comparator_trigger_is_pending(TEST_DEV) != 0) {
			return false;
		}
	} else {
		if (pending != 0) {
			return false;
		}
	}

	return true;
}

/* ------------------------------------------------------------------ */
/*  Per-mode single-transition steps. Each returns true on success.     */
/* ------------------------------------------------------------------ */
static bool step_rising(void)
{
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_RISING_EDGE);
	comparator_set_trigger_callback(TEST_DEV, acomp_cb, NULL);

	drive(0);
	flush_cb();

	/* Rising edge must produce exactly one callback. */
	uint32_t before = atomic_get(&st.cb_hits);
	drive(1);
	if (!check_cb_delta(before, true)) {
		return false;
	}
	/* Opposite (falling) edge must NOT produce a callback in rising mode. */
	before = atomic_get(&st.cb_hits);
	drive(0);
	if (!check_cb_delta(before, false)) {
		return false;
	}
	flush_cb();

	return true;
}

static bool step_falling(void)
{
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_FALLING_EDGE);
	comparator_set_trigger_callback(TEST_DEV, acomp_cb, NULL);

	drive(0);
	flush_cb();

	/* Rising edge must NOT trigger in falling mode. */
	uint32_t before = atomic_get(&st.cb_hits);
	drive(1);
	if (!check_cb_delta(before, false)) {
		return false;
	}
	/* Falling edge must produce exactly one callback. */
	before = atomic_get(&st.cb_hits);
	drive(0);
	if (!check_cb_delta(before, true)) {
		return false;
	}
	flush_cb();

	return true;
}

static bool step_both(void)
{
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_BOTH_EDGES);
	comparator_set_trigger_callback(TEST_DEV, acomp_cb, NULL);

	drive(0);
	flush_cb();

	/* Rising edge produces one callback. */
	uint32_t before = atomic_get(&st.cb_hits);
	drive(1);
	if (!check_cb_delta(before, true)) {
		return false;
	}
	/* Falling edge produces one callback. */
	before = atomic_get(&st.cb_hits);
	drive(0);
	if (!check_cb_delta(before, true)) {
		return false;
	}
	flush_cb();

	return true;
}

static bool step_pending(void)
{
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_NONE);
	comparator_set_trigger_callback(TEST_DEV, NULL, NULL);

	drive(0);
	/* Clear any stale pending. */
	(void)comparator_trigger_is_pending(TEST_DEV);

	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_RISING_EDGE);
	/* Rising edge must pend (read 1 then clear 0). */
	drive(1);
	if (!pending_read_clear(true)) {
		return false;
	}
	/* Falling edge must not pend in rising mode. */
	drive(0);
	if (!pending_read_clear(false)) {
		return false;
	}
	(void)comparator_trigger_is_pending(TEST_DEV);

	return true;
}

static bool step_rearm(void)
{
	/* Phase A: trigger NONE -> no stale event. */
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_NONE);
	comparator_set_trigger_callback(TEST_DEV, NULL, NULL);
	drive(0);
	(void)comparator_trigger_is_pending(TEST_DEV);
	flush_cb();

	uint32_t before_cb = atomic_get(&st.cb_hits);

	drive(1);
	k_usleep(SETTLE_US);
	if (comparator_trigger_is_pending(TEST_DEV) != 0) {
		return false;
	}
	/* No callback may fire while trigger is NONE. */
	if (atomic_get(&st.cb_hits) != before_cb) {
		return false;
	}

	/* Phase B: re-arm rising at low and ensure the next edge is NOT lost. */
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_RISING_EDGE);
	comparator_set_trigger_callback(TEST_DEV, acomp_cb, NULL);
	drive(0);
	flush_cb();
	before_cb = atomic_get(&st.cb_hits);

	drive(1);
	if (!check_cb_delta(before_cb, true)) {
		return false;
	}
	if (atomic_get(&st.cb_hits) == before_cb) {
		/* Redundant guard: a valid callback must have incremented. */
		return false;
	}
	drive(0);
	flush_cb();

	return true;
}

/* ------------------------------------------------------------------ */
/*  Ztress handlers                                                     */
/* ------------------------------------------------------------------ */
static bool acomp_owner(void *user_data, uint32_t cnt, bool last, int prio)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(prio);

	if (last) {
		/* Idle the hardware before the next phase. */
		comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_NONE);
		comparator_set_trigger_callback(TEST_DEV, NULL, NULL);
		gpio_pin_set_dt(&TEST_PIN, 0);
		return true;
	}

	bool ok;

	switch (g_mode) {
	case MODE_RISING:
		ok = step_rising();
		break;
	case MODE_FALLING:
		ok = step_falling();
		break;
	case MODE_BOTH:
		ok = step_both();
		break;
	case MODE_PENDING:
		ok = step_pending();
		break;
	case MODE_REARM:
		ok = step_rearm();
		break;
	default:
		ok = false;
		break;
	}

	if (ok) {
		atomic_inc(&st.transitions);
	} else {
		atomic_inc(&st.fails);
	}

	/* Acknowledge the timer's request token. */
	atomic_set(&st.requested, 0);

	return true;
}

/* ISR-context timer handler: ISR-safe, no blocking, no allocations. */
static bool acomp_timer(void *user_data, uint32_t cnt, bool last, int prio)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(prio);

	if (last) {
		return false;
	}

	atomic_inc(&st.epoch);
	atomic_set(&st.requested, 1);
	return true;
}

/* Scheduler contention only; never touches GPIO or comparator. */
static bool cpu_noise(void *user_data, uint32_t cnt, bool last, int prio)
{
	volatile uint32_t d;

	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(prio);

	if (last) {
		return false;
	}

	for (int i = 0; i < 200; i++) {
		d = (d + 1) & 0xff;
	}
	(void)d;
	k_busy_wait(200);
	return true;
}

/* ------------------------------------------------------------------ */
/*  Phase runner                                                        */
/* ------------------------------------------------------------------ */
#define OWNER_ITER 1200
#define MIN_TRANSITIONS 1000
#define PHASE_TIMEOUT_MS 20000

static void reset_state(void)
{
	atomic_set(&st.cb_hits, 0);
	atomic_set(&st.transitions, 0);
	atomic_set(&st.fails, 0);
	atomic_set(&st.epoch, 0);
	atomic_set(&st.requested, 0);
	k_sem_reset(&cb_sem);
}

static void run_phase(enum phase_mode mode)
{
	g_mode = mode;
	reset_state();

	/* Put hardware in a known idle state. */
	comparator_set_trigger(TEST_DEV, COMPARATOR_TRIGGER_NONE);
	comparator_set_trigger_callback(TEST_DEV, NULL, NULL);
	gpio_pin_set_dt(&TEST_PIN, 0);
	k_msleep(2);

	ztress_set_timeout(K_MSEC(PHASE_TIMEOUT_MS));

	ZTRESS_EXECUTE(
		ZTRESS_TIMER(acomp_timer, NULL, 0, K_MSEC(1)),
		ZTRESS_THREAD(acomp_owner, NULL, OWNER_ITER, 0, K_MSEC(1)),
		ZTRESS_THREAD(cpu_noise, NULL, 0, 0, K_MSEC(1)));

	ztress_set_timeout(K_NO_WAIT);

	uint32_t transitions = atomic_get(&st.transitions);
	uint32_t fails = atomic_get(&st.fails);

	printk("phase mode=%d transitions=%u fails=%u\n", (int)mode, transitions, fails);
	zassert_equal(fails, 0, "phase %d: %u validation failures", (int)mode, fails);
	zassert_true(transitions >= MIN_TRANSITIONS,
		     "phase %d: only %u transitions (< %u)", (int)mode, transitions,
		     MIN_TRANSITIONS);
}

/* ------------------------------------------------------------------ */
/*  Ztest suite                                                         */
/* ------------------------------------------------------------------ */
static void *test_setup(void)
{
	zassert_true(device_is_ready(TEST_DEV), "comparator not ready");
	zassert_true(gpio_is_ready_dt(&TEST_PIN), "gpio not ready");
	zassert_ok(gpio_pin_configure_dt(&TEST_PIN, GPIO_OUTPUT_INACTIVE));
	return NULL;
}

ZTEST_SUITE(comparator_ztress, NULL, test_setup, NULL, NULL, NULL);

ZTEST(comparator_ztress, test_rising_callback_stress)
{
	run_phase(MODE_RISING);
}

ZTEST(comparator_ztress, test_falling_callback_stress)
{
	run_phase(MODE_FALLING);
}

ZTEST(comparator_ztress, test_both_edges_callback_stress)
{
	run_phase(MODE_BOTH);
}

ZTEST(comparator_ztress, test_pending_stress)
{
	run_phase(MODE_PENDING);
}

ZTEST(comparator_ztress, test_rearm_stress)
{
	run_phase(MODE_REARM);
}
