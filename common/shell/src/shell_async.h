/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * shell_async — run shell command handlers in a background thread.
 *
 * Usage:
 *   static int my_real_work(const struct shell *sh, size_t argc, char **argv)
 *   {
 *       k_sleep(K_MSEC(500));           // long operation — won't block shell
 *       log_print(sh, "Done!");
 *       return 0;
 *   }
 *
 *   static int cmd_run(const struct shell *sh, size_t argc, char **argv)
 *   {
 *       return shell_async_submit(sh, argc, argv, my_real_work);
 *   }
 *   SHELL_CMD_REGISTER(run, NULL, "Run async", cmd_run);
 */

#ifndef SHELL_ASYNC_H_
#define SHELL_ASYNC_H_

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Prototype of the actual work function executed in the background thread. */
typedef int (*shell_async_fn)(const struct shell *sh,
			      size_t argc, char **argv);

/**
 * @brief Submit a shell command for background execution.
 *
 * The caller (shell command handler) returns immediately. The real work
 * is dispatched to a dedicated background thread and the shell prompt
 * becomes available right away.
 *
 * @param sh    Current shell instance (output will be directed here).
 * @param argc  Argument count (forwarded from the shell handler).
 * @param argv  Argument vector (copied internally, safe after return).
 * @param fn    Function to execute in the background thread.
 *
 * @retval 0           Work submitted successfully.
 * @retval -EBUSY      Background thread slot is occupied (try again later).
 * @retval -ENOMEM     Too many arguments to copy.
 */
int shell_async_submit(const struct shell *sh, size_t argc, char **argv,
		       shell_async_fn fn);

/**
 * @brief Check if a background job is currently running.
 *
 * @retval true   A job is in progress.
 * @retval false  Idle.
 */
bool shell_async_busy(void);

/**
 * @brief Abort the currently running background job (best-effort).
 *
 * Sets a flag that the job function should check via shell_async_aborted().
 */
void shell_async_abort(void);

/**
 * @brief Check if abort has been requested (called from inside the job fn).
 *
 * @retval true  Caller should stop and return.
 */
bool shell_async_aborted(void);

/**
 * @brief Get the worker ID of the calling thread (0 / 1 / 2 ...).
 *
 * Call this from inside a job function to identify which worker is running.
 *
 * @retval 0..N-1  Worker index.
 * @retval -1      Called from outside a worker thread.
 */
int shell_async_worker_id(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_ASYNC_H_ */
