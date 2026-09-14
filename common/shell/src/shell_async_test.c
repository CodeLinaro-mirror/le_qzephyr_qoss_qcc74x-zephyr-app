/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * async_test — verification commands for shell_async module.
 *
 * Commands:
 *   async_test run             run 5-step async task (500ms per step)
 *   async_test run_long        run 10-step async task (1s per step)
 *   async_test abort           abort current async task
 *   async_test busy            check if async task is running
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "shell_async.h"

/* ------------------------------------------------------------------ */
/*  Worker functions (executed in background thread or sync context)    */
/* ------------------------------------------------------------------ */

/**
 * Shared worker for short task (5 steps x 500ms).
 * When async (wid >= 0): output via shell_print with [async#N] prefix.
 * When sync  (wid < 0):  output via shell_print (original behavior).
 */
static int async_short_work(const struct shell *sh, size_t argc, char **argv)
{
	int wid = shell_async_worker_id();

	shell_print(sh, "[async#%d] short task started (5 steps x 500ms)", wid);

	for (int i = 1; i <= 5; i++) {
		if (shell_async_aborted()) {
			shell_print(sh, "[async#%d] ABORTED at step %d", wid, i);
			return -ECANCELED;
		}
		shell_print(sh, "[async#%d] step %d/5", wid, i);
		k_sleep(K_MSEC(500));
	}

	shell_print(sh, "[async#%d] short task DONE", wid);
	return 0;
}

/**
 * Shared worker for long task (10 steps x 1s).
 * When async (wid >= 0): output via shell_print with [async#N] prefix.
 * When sync  (wid < 0):  output via shell_print (original behavior).
 */
static int async_long_work(const struct shell *sh, size_t argc, char **argv)
{
	int wid = shell_async_worker_id();

	shell_print(sh, "[async#%d] long task started (10 steps x 1s)", wid);

	for (int i = 1; i <= 10; i++) {
		if (shell_async_aborted()) {
			shell_print(sh, "[async#%d] ABORTED at step %d", wid, i);
			return -ECANCELED;
		}
		shell_print(sh, "[async#%d] step %d/10", wid, i);
		k_sleep(K_SECONDS(1));
	}

	shell_print(sh, "[async#%d] long task DONE", wid);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Shell command handlers                                               */
/* ------------------------------------------------------------------ */

/**
 * async_test run [&]
 * Without &: run synchronously (shell blocked until done)
 * With &:    run asynchronously (shell returns immediately)
 */
static int cmd_run(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1 && strcmp(argv[argc - 1], "&") == 0) {
		/* Async mode: submit to worker thread */
		argc--;
		return shell_async_submit(sh, argc, argv, async_short_work);
	}
	/* Sync mode: call directly in shell thread */
	return async_short_work(sh, argc, argv);
}

/**
 * async_test run_long [&]
 * Without &: run synchronously (shell blocked until done)
 * With &:    run asynchronously (shell returns immediately)
 */
static int cmd_run_long(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1 && strcmp(argv[argc - 1], "&") == 0) {
		/* Async mode: submit to worker thread */
		argc--;
		return shell_async_submit(sh, argc, argv, async_long_work);
	}
	/* Sync mode: call directly in shell thread */
	return async_long_work(sh, argc, argv);
}

static int cmd_abort(const struct shell *sh, size_t argc, char **argv)
{
	if (!shell_async_busy()) {
		shell_print(sh, "[async] nothing running");
		return 0;
	}
	shell_async_abort();
	shell_print(sh, "[async] abort requested");
	return 0;
}

static int cmd_busy(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "[async] busy: %s",
		    shell_async_busy() ? "YES" : "NO");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Registration                                                         */
/* ------------------------------------------------------------------ */

SHELL_STATIC_SUBCMD_SET_CREATE(async_test_cmds,
	SHELL_CMD_ARG(run,      NULL, "Run 5-step async task (2.5s total)",
		      cmd_run,      1, 1),
	SHELL_CMD_ARG(run_long, NULL, "Run 10-step async task (10s total)",
		      cmd_run_long, 1, 1),
	SHELL_CMD_ARG(abort,    NULL, "Abort all running async tasks",
		      cmd_abort,    1, 0),
	SHELL_CMD_ARG(busy,     NULL, "Check if any async task is running",
		      cmd_busy,     1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((qcom), async_test, &async_test_cmds, "shell_async verification commands", NULL, 1, 0);
