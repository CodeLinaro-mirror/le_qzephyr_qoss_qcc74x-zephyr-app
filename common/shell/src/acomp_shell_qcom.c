/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCOM comparator ("acomp") shell command - comprehensive QCC74x comparator
 * verification through the Zephyr comparator driver API.
 *
 * Zephyr comparator API covered (see zephyr/include/zephyr/drivers/comparator.h):
 *   - comparator_get_output()             -> "qcom acomp output <dev>"
 *   - comparator_set_trigger()            -> "qcom acomp trigger <dev> <mode>"
 *   - comparator_set_trigger_callback()   -> "qcom acomp callback <dev> <on|off>"
 *   - comparator_trigger_is_pending()     -> "qcom acomp pending <dev>"
 * Also provides end-to-end self tests that exercise all four APIs together:
 *   - "qcom acomp await <dev> <mode> [s]" - arm callback+trigger and wait
 *   - "qcom acomp loop <dev> <iters> <ms>" - toggle drive GPIO, verify output
 *   - "qcom acomp selftest <dev> <iters>"  - combined output+callback+pending test
 *
 * Device selection: <dev> is 0|1 corresponding to the enabled
 * qcom,qcc74x-acomp nodes in devicetree order (comparator0 -> 0,
 * comparator1 -> 1).  Both QCC74x comparators share the same HBN_OUT1 IRQ;
 * each device has its own Zephyr driver instance and its own trigger
 * callback/pending state.
 *
 * A single acomp_service thread is the SOLE owner of the comparator hardware
 * and (optionally) the drive GPIO.  Shell command handlers only parse/validate
 * input and enqueue a request to that thread, so they never block the prompt
 * and there is no race between shell commands and the ISR callback.
 *
 * To keep this module compilable in applications that do not instantiate a
 * comparator, all hardware references are guarded by devicetree checks.  The
 * optional drive GPIO is read from "zephyr,user.acomp-drive-gpios".
 */

#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(acomp_shell, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Devicetree bindings (compile-time guarded)                          */
/* ------------------------------------------------------------------ */

#define HAS_COMPARATOR DT_HAS_COMPAT_STATUS_OKAY(qcom_qcc74x_acomp)
#define DRIVE_PROP acomp_drive_gpios

#if HAS_COMPARATOR || DT_NODE_HAS_PROP(DT_PATH(zephyr_user), DRIVE_PROP)

#if HAS_COMPARATOR
/* Build an index-ordered table of enabled comparator devices. */
struct acomp_dev_ent {
	const struct device *dev;
	const char *name;
};

#define ACOMP_DEVS_ENTRY(node) \
	{ .dev = DEVICE_DT_GET(node), .name = DT_NODE_FULL_NAME(node) },

static const struct acomp_dev_ent acomp_devs[] = {
	DT_FOREACH_STATUS_OKAY(qcom_qcc74x_acomp, ACOMP_DEVS_ENTRY)
};

#define ACOMP_NUM_DEVS ARRAY_SIZE(acomp_devs)

/*
 * QCC74x exposes at most 2 comparators (comparator0, comparator1).  One
 * callback semaphore per device lets "await"/"selftest" wait on a specific
 * device's ISR callback.
 */
#define ACOMP_MAX_DEVS 2
static struct k_sem acomp_cb_sems[ACOMP_MAX_DEVS];
static bool acomp_cb_sems_inited;
#endif /* HAS_COMPARATOR */

/* Optional drive GPIO. */
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), DRIVE_PROP)
static const struct gpio_dt_spec acomp_drive_pin =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), DRIVE_PROP);
#define ACOMP_DRIVE_GPIO acomp_drive_pin
#define HAS_DRIVE_GPIO 1
#else
#define ACOMP_DRIVE_GPIO NULL
#define HAS_DRIVE_GPIO 0
#endif

#define ACOMP_INVALID_DEV 0xFFU
#define ACOMP_DRIVE_SETTLE_US 200U
#define ACOMP_SERVICE_STACK_SIZE 256
#define ACOMP_SERVICE_PRIORITY 6
#define ACOMP_MSGQ_DEPTH 4
#define ACOMP_DEFAULT_AWAIT_TIMEOUT_S 10

/* ------------------------------------------------------------------ */
/*  Request plumbing                                                     */
/* ------------------------------------------------------------------ */

enum acomp_op {
	OP_LIST,
	OP_STATUS,
	OP_OUTPUT,
	OP_TRIGGER,
	OP_CALLBACK,
	OP_PENDING,
	OP_AWAIT,
	OP_DRIVE,
	OP_LOOP,
	OP_SELFTEST,
	OP_COUNT,
};

struct acomp_req {
	enum acomp_op op;
	const struct shell *sh;
	uint8_t idx;     /* device index, ACOMP_INVALID_DEV = none */
	uint32_t a;      /* op-specific parameter 1 */
	uint32_t b;      /* op-specific parameter 2 */
	uint32_t c;      /* op-specific parameter 3 */
};

K_MSGQ_DEFINE(acomp_msgq, sizeof(struct acomp_req), ACOMP_MSGQ_DEPTH, 4);

/* ISR-safe callback: no GPIO/comparator calls, no blocking, no allocation. */
static void acomp_cb(const struct device *dev, void *user_data)
{
	uintptr_t idx = (uintptr_t)user_data;

	ARG_UNUSED(dev);
	if (idx < MIN(ACOMP_NUM_DEVS, ACOMP_MAX_DEVS)) {
		k_sem_give(&acomp_cb_sems[idx]);
	}
}

/* Init per-device callback semaphores once (idempotent, capped at max). */
static void acomp_sems_ensure_inited(void)
{
	if (!acomp_cb_sems_inited) {
		for (uint8_t i = 0; i < MIN(ACOMP_NUM_DEVS, ACOMP_MAX_DEVS); i++) {
			k_sem_init(&acomp_cb_sems[i], 0, 1);
		}
		acomp_cb_sems_inited = true;
	}
}

#if HAS_DRIVE_GPIO
/* PDS15 resets the pin mux, so restore the output configuration on every use. */
static int acomp_drive_set(uint32_t level)
{
	gpio_flags_t flags = level != 0U ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE;

	return gpio_pin_configure_dt(&ACOMP_DRIVE_GPIO, flags);
}
#endif /* HAS_DRIVE_GPIO */

/* ------------------------------------------------------------------ */
/*  Service thread: sole hardware owner                                  */
/* ------------------------------------------------------------------ */

static const struct device *acomp_dev_get(uint8_t idx, const struct shell *sh)
{
	if (idx >= ACOMP_NUM_DEVS) {
		shell_error(sh, "comparator %u out of range (0..%u)",
			    idx, ACOMP_NUM_DEVS - 1);
		return NULL;
	}

	return acomp_devs[idx].dev;
}

/* Parse <dev> argument; returns index or ACOMP_INVALID_DEV on error. */
static uint8_t acomp_parse_dev(const struct shell *sh, const char *s)
{
	char *end;
	long v;

	if (ACOMP_NUM_DEVS == 0) {
		shell_error(sh, "no comparator enabled "
			    "(enable qcom,qcc74x-acomp nodes)");
		return ACOMP_INVALID_DEV;
	}

	v = strtol(s, &end, 10);
	if (*end != '\0' || v < 0 || v >= (long)ACOMP_NUM_DEVS) {
		shell_error(sh, "invalid device '%s' (0..%u)",
			    s, ACOMP_NUM_DEVS - 1);
		return ACOMP_INVALID_DEV;
	}

	return (uint8_t)v;
}

static const char *trigger_mode_str(uint32_t mode)
{
	switch (mode) {
	case COMPARATOR_TRIGGER_NONE:
		return "none";
	case COMPARATOR_TRIGGER_RISING_EDGE:
		return "rising";
	case COMPARATOR_TRIGGER_FALLING_EDGE:
		return "falling";
	case COMPARATOR_TRIGGER_BOTH_EDGES:
		return "both";
	default:
		return "?";
	}
}

static void service_exec(const struct acomp_req *req)
{
	const struct shell *sh = req->sh;

	switch (req->op) {
	case OP_LIST: {
		shell_print(sh, "acomp devices: %u", ACOMP_NUM_DEVS);

		for (uint8_t i = 0; i < ACOMP_NUM_DEVS; i++) {
			shell_print(sh, "  [%u] %-10s present=%d ready=%d",
				    i, acomp_devs[i].name,
				    (int)(acomp_devs[i].dev != NULL),
				    device_is_ready(acomp_devs[i].dev));
		}
#if HAS_DRIVE_GPIO
		shell_print(sh, "drive gpio: present");
#else
		shell_print(sh, "drive gpio: not configured "
			    "(add zephyr,user.acomp-drive-gpios)");
#endif
		break;
	}

	case OP_STATUS:
	case OP_OUTPUT:
	case OP_TRIGGER:
	case OP_CALLBACK:
	case OP_PENDING:
	case OP_LOOP:
	case OP_SELFTEST: {
		const struct device *dev = acomp_dev_get(req->idx, sh);

		if (dev == NULL) {
			return;
		}
		if (!device_is_ready(dev)) {
			shell_error(sh, "%s not ready", acomp_devs[req->idx].name);
			return;
		}

		if (req->op == OP_STATUS) {
			int out = comparator_get_output(dev);

			shell_print(sh, "device[%u] %s", req->idx, acomp_devs[req->idx].name);
			shell_print(sh, "  ready=%d", device_is_ready(dev));
			shell_print(sh, "  output=%u",
				    out < 0 ? (unsigned)-1 : (unsigned)out);
			break;
		}

		if (req->op == OP_OUTPUT) {
			int out = comparator_get_output(dev);

			if (out < 0) {
				shell_error(sh, "comparator_get_output failed: %d", out);
			} else {
				shell_print(sh, "output: %d", out);
			}
			break;
		}

		if (req->op == OP_TRIGGER) {
			int ret = comparator_set_trigger(dev, (enum comparator_trigger)req->a);

			if (ret < 0) {
				shell_error(sh, "comparator_set_trigger failed: %d", ret);
			} else {
				shell_print(sh, "trigger set: %s",
					    trigger_mode_str(req->a));
			}
			break;
		}

		if (req->op == OP_CALLBACK) {
			int ret = comparator_set_trigger_callback(
				dev, req->a ? acomp_cb : NULL,
				req->a ? (void *)(uintptr_t)req->idx : NULL);

			if (ret < 0) {
				shell_error(sh, "comparator_set_trigger_callback failed: %d", ret);
			} else {
				shell_print(sh, "callback %s (device %u)",
					    req->a ? "on" : "off", req->idx);
			}
			break;
		}

		if (req->op == OP_PENDING) {
			int pending = comparator_trigger_is_pending(dev);

			if (pending < 0) {
				shell_error(sh, "comparator_trigger_is_pending failed: %d",
					    pending);
			} else {
				shell_print(sh, "pending(read+clear): %d", pending);
			}
			break;
		}

		/* OP_LOOP and OP_SELFTEST need the drive GPIO. */
#if HAS_DRIVE_GPIO
		if (req->op == OP_LOOP) {
			uint32_t iters = req->a;
			uint32_t delay = req->b;
			k_timeout_t dly = K_MSEC(delay);

			int ret = acomp_drive_set(0);

			if (ret < 0) {
				shell_error(sh, "gpio_pin_configure_dt failed: %d", ret);
				break;
			}

			int prev = comparator_get_output(dev);
			uint32_t changes = 0;
			uint32_t bad = 0;

			for (uint32_t i = 0; i < iters; i++) {
				ret = acomp_drive_set(i & 1);
				if (ret < 0) {
					bad++;
					break;
				}
				k_sleep(dly);
				ret = acomp_drive_set(i & 1);
				if (ret < 0) {
					bad++;
					break;
				}
				k_busy_wait(ACOMP_DRIVE_SETTLE_US);
				int cur = comparator_get_output(dev);
				if (cur < 0) {
					bad++;
					break;
				}
				if (cur != prev) {
					changes++;
					prev = cur;
				}
			}
			shell_print(sh, "loop: %u iters, %u output changes, %u errors",
				    iters, changes, bad);
			break;
		}

		if (req->op == OP_SELFTEST) {
			uint32_t iters = req->a;

			acomp_sems_ensure_inited();
			int ret = acomp_drive_set(0);

			if (ret < 0) {
				shell_error(sh, "gpio_pin_configure_dt failed: %d", ret);
				break;
			}

			/* Re-arm: clear callback sem + pending, set both edges. */
			k_sem_reset(&acomp_cb_sems[req->idx]);
			(void)comparator_trigger_is_pending(dev);
			ret = comparator_set_trigger_callback(
				dev, acomp_cb, (void *)(uintptr_t)req->idx);
			if (ret < 0) {
				shell_error(sh, "set_trigger_callback failed: %d", ret);
				break;
			}
			ret = comparator_set_trigger(dev, COMPARATOR_TRIGGER_BOTH_EDGES);
			if (ret < 0) {
				shell_error(sh, "set_trigger failed: %d", ret);
				break;
			}

			uint32_t callbacks = 0;
			uint32_t changes = 0;
			int prev = comparator_get_output(dev);

			for (uint32_t i = 0; i < iters; i++) {
				ret = acomp_drive_set(i & 1);
				if (ret < 0) {
					break;
				}
				k_sleep(K_USEC(200));
				ret = acomp_drive_set(i & 1);
				if (ret < 0) {
					break;
				}
				k_busy_wait(ACOMP_DRIVE_SETTLE_US);
				int cur = comparator_get_output(dev);
				if (cur < 0) {
					break;
				}
				if (cur != prev) {
					changes++;
					prev = cur;
				}
				/* Drain any callback that fired this edge. */
				while (k_sem_take(&acomp_cb_sems[req->idx], K_NO_WAIT) == 0) {
					callbacks++;
				}
			}

			/* Disarm. */
			(void)comparator_set_trigger(dev, COMPARATOR_TRIGGER_NONE);
			(void)comparator_set_trigger_callback(dev, NULL, NULL);

			int pending = comparator_trigger_is_pending(dev);
			shell_print(sh, "selftest dev%u: iters=%u toggles=%u output_changes=%u "
				    "callbacks=%u leftover_pending=%d",
				    req->idx, iters, iters, changes, callbacks, pending);

			/*
			 * Oracle: with both-edge trigger, every internal transition of
			 * the comparator output must reach the ISR callback.  The
			 * 0,1,0,1 drive pattern yields at most iters-1 output changes,
			 * so require the observed callback count to match that.  A
			 * leftover pending flag means an edge was missed by the
			 * callback path (fail).
			 */
			bool ok = (callbacks >= iters - 1) && (pending == 0);

			if (ret < 0) {
				shell_print(sh, "selftest: RESULT FAIL (gpio error)");
			} else {
				shell_print(sh, "selftest: RESULT %s",
					    ok ? "PASS" : "FAIL");
			}
			break;
		}
#else
		shell_error(sh, "no drive GPIO configured "
			    "(add zephyr,user.acomp-drive-gpios)");
		break;
#endif /* HAS_DRIVE_GPIO */
		break;
	}

	case OP_AWAIT: {
		const struct device *dev = acomp_dev_get(req->idx, sh);

		if (dev == NULL) {
			return;
		}
		if (!device_is_ready(dev)) {
			shell_error(sh, "%s not ready", acomp_devs[req->idx].name);
			return;
		}
		acomp_sems_ensure_inited();

		k_sem_reset(&acomp_cb_sems[req->idx]);
		(void)comparator_trigger_is_pending(dev);

		int ret = comparator_set_trigger_callback(
			dev, acomp_cb, (void *)(uintptr_t)req->idx);
		if (ret < 0) {
			shell_error(sh, "set_trigger_callback failed: %d", ret);
			return;
		}
		ret = comparator_set_trigger(dev, (enum comparator_trigger)req->a);
		if (ret < 0) {
			shell_error(sh, "set_trigger failed: %d", ret);
			(void)comparator_set_trigger_callback(dev, NULL, NULL);
			return;
		}

		shell_print(sh, "await dev%u mode=%s timeout=%us ...",
			    req->idx, trigger_mode_str(req->a), req->b);
		ret = k_sem_take(&acomp_cb_sems[req->idx], K_SECONDS(req->b));

		/* Disarm. */
		(void)comparator_set_trigger(dev, COMPARATOR_TRIGGER_NONE);
		(void)comparator_set_trigger_callback(dev, NULL, NULL);

		if (ret == 0) {
			shell_print(sh, "await: triggered");
		} else {
			shell_print(sh, "await: timed out");
		}
		break;
	}

	case OP_DRIVE: {
#if HAS_DRIVE_GPIO
		int ret = acomp_drive_set(req->a);

		if (ret < 0) {
			shell_error(sh, "gpio_pin_configure_dt failed: %d", ret);
			break;
		}
		shell_print(sh, "drive gpio -> %u", req->a);
#else
		shell_error(sh, "no drive GPIO configured "
			    "(add zephyr,user.acomp-drive-gpios)");
#endif
		break;
	}

	default:
		shell_error(sh, "invalid acomp op");
		break;
	}
}

static void acomp_service_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct acomp_req req;

	while (k_msgq_get(&acomp_msgq, &req, K_FOREVER) == 0) {
		service_exec(&req);
	}
}

K_THREAD_DEFINE(acomp_service, ACOMP_SERVICE_STACK_SIZE, acomp_service_thread,
		NULL, NULL, NULL, ACOMP_SERVICE_PRIORITY, 0, 0);

/* ------------------------------------------------------------------ */
/*  Shell command handlers (enqueue only)                                */
/* ------------------------------------------------------------------ */

static int enqueue(enum acomp_op op, const struct shell *sh, uint8_t idx,
		   uint32_t a, uint32_t b, uint32_t c)
{
	struct acomp_req req = {
		.op = op,
		.sh = sh,
		.idx = idx,
		.a = a,
		.b = b,
		.c = c,
	};

	int ret = k_msgq_put(&acomp_msgq, &req, K_NO_WAIT);

	if (ret != 0) {
		shell_warn(sh, "acomp service queue full, command dropped (%d)", ret);
	}

	return ret;
}

static int cmd_acomp_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return enqueue(OP_LIST, sh, ACOMP_INVALID_DEV, 0, 0, 0);
}

static int cmd_acomp_status(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}
	return enqueue(OP_STATUS, sh, idx, 0, 0, 0);
}

static int cmd_acomp_output(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}
	return enqueue(OP_OUTPUT, sh, idx, 0, 0, 0);
}

static int cmd_acomp_trigger(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);
	const char *name = argv[2];
	uint32_t v;

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}

	if (!strcmp(name, "none")) {
		v = COMPARATOR_TRIGGER_NONE;
	} else if (!strcmp(name, "rising")) {
		v = COMPARATOR_TRIGGER_RISING_EDGE;
	} else if (!strcmp(name, "falling")) {
		v = COMPARATOR_TRIGGER_FALLING_EDGE;
	} else if (!strcmp(name, "both")) {
		v = COMPARATOR_TRIGGER_BOTH_EDGES;
	} else {
		shell_error(sh, "invalid trigger '%s' (none|rising|falling|both)", name);
		return -EINVAL;
	}

	return enqueue(OP_TRIGGER, sh, idx, v, 0, 0);
}

static int cmd_acomp_callback(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);
	const char *name = argv[2];

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}

	if (!strcmp(name, "on")) {
		return enqueue(OP_CALLBACK, sh, idx, 1, 0, 0);
	}
	if (!strcmp(name, "off")) {
		return enqueue(OP_CALLBACK, sh, idx, 0, 0, 0);
	}

	shell_error(sh, "invalid callback '%s' (on|off)", name);
	return -EINVAL;
}

static int cmd_acomp_pending(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}
	return enqueue(OP_PENDING, sh, idx, 0, 0, 0);
}

static int cmd_acomp_await(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);
	const char *name = argv[2];
	uint32_t mode;

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}

	if (!strcmp(name, "rising")) {
		mode = COMPARATOR_TRIGGER_RISING_EDGE;
	} else if (!strcmp(name, "falling")) {
		mode = COMPARATOR_TRIGGER_FALLING_EDGE;
	} else if (!strcmp(name, "both")) {
		mode = COMPARATOR_TRIGGER_BOTH_EDGES;
	} else {
		shell_error(sh, "invalid mode '%s' (rising|falling|both)", name);
		return -EINVAL;
	}

	long timeout_s = (argc >= 4) ? strtol(argv[3], NULL, 10)
				      : ACOMP_DEFAULT_AWAIT_TIMEOUT_S;
	if (timeout_s < 1 || timeout_s > 3600) {
		shell_error(sh, "timeout must be in [1,3600] seconds");
		return -EINVAL;
	}

	return enqueue(OP_AWAIT, sh, idx, mode, (uint32_t)timeout_s, 0);
}

static int cmd_acomp_drive(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t v;

	if (!strcmp(argv[1], "0")) {
		v = 0;
	} else if (!strcmp(argv[1], "1")) {
		v = 1;
	} else {
		shell_error(sh, "invalid level '%s' (0|1)", argv[1]);
		return -EINVAL;
	}

	return enqueue(OP_DRIVE, sh, ACOMP_INVALID_DEV, v, 0, 0);
}

static int cmd_acomp_loop(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);
	long iters = strtol(argv[2], NULL, 10);
	long delay = strtol(argv[3], NULL, 10);

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}
	if (iters < 1 || iters > 1000000) {
		shell_error(sh, "iters must be in [1,1000000]");
		return -EINVAL;
	}
	if (delay < 1 || delay > 10000) {
		shell_error(sh, "delay_ms must be in [1,10000]");
		return -EINVAL;
	}

	return enqueue(OP_LOOP, sh, idx, (uint32_t)iters, (uint32_t)delay, 0);
}

static int cmd_acomp_selftest(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = acomp_parse_dev(sh, argv[1]);
	long iters = strtol(argv[2], NULL, 10);

	if (idx == ACOMP_INVALID_DEV) {
		return -EINVAL;
	}
	if (iters < 1 || iters > 1000000) {
		shell_error(sh, "iters must be in [1,1000000]");
		return -EINVAL;
	}

	return enqueue(OP_SELFTEST, sh, idx, (uint32_t)iters, 0, 0);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_acomp,
	SHELL_CMD_ARG(list, NULL, "List comparator devices and drive GPIO",
		      cmd_acomp_list, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show device state: qcom acomp status <dev>",
		      cmd_acomp_status, 2, 0),
	SHELL_CMD_ARG(output, NULL, "Read output: qcom acomp output <dev>",
		      cmd_acomp_output, 2, 0),
	SHELL_CMD_ARG(trigger, NULL,
		      "Set trigger: qcom acomp trigger <dev> <none|rising|falling|both>",
		      cmd_acomp_trigger, 3, 0),
	SHELL_CMD_ARG(callback, NULL,
		      "Set callback: qcom acomp callback <dev> <on|off>",
		      cmd_acomp_callback, 3, 0),
	SHELL_CMD_ARG(pending, NULL, "Read-and-clear pending: qcom acomp pending <dev>",
		      cmd_acomp_pending, 2, 0),
	SHELL_CMD_ARG(await, NULL,
		      "Arm & wait for edge: qcom acomp await <dev> <rising|falling|both> [s]",
		      cmd_acomp_await, 3, 1),
	SHELL_CMD_ARG(drive, NULL, "Drive GPIO pad: qcom acomp drive <0|1>",
		      cmd_acomp_drive, 2, 0),
	SHELL_CMD_ARG(loop, NULL,
		      "Toggle GPIO & read output: qcom acomp loop <dev> <iters> <delay_ms>",
		      cmd_acomp_loop, 4, 0),
	SHELL_CMD_ARG(selftest, NULL,
		      "End-to-end self test: qcom acomp selftest <dev> <iters>",
		      cmd_acomp_selftest, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((qcom), acomp, &sub_acomp, "comparator commands", NULL, 1, 0);

#endif /* HAS_COMPARATOR || HAS_DRIVE_GPIO */
