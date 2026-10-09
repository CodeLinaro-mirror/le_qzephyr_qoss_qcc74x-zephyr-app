/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * shell_async — dynamic thread-pool for background shell command execution.
 *
 * Each submit() allocates a thread stack from the system heap and creates
 * a worker thread.  When the worker finishes its job it submits a k_work
 * item to the system workqueue; that handler calls k_thread_join() (the
 * thread has already returned so this returns almost immediately) then
 * k_thread_stack_free() to release the heap memory.  The slot is marked
 * free and ready for the next submit().
 *
 * This avoids the "lazy-free on next submit" approach: stacks are released
 * as soon as the work handler runs, typically within one system tick.
 *
 * Requires:
 *   CONFIG_DYNAMIC_THREAD=y
 *   CONFIG_DYNAMIC_THREAD_ALLOC=y
 *   CONFIG_QCOM_SHELL_ASYNC=y
 */

#include "shell_async.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(shell_async, LOG_LEVEL_WRN);

/* ------------------------------------------------------------------ */
/*  Configuration                                                        */
/* ------------------------------------------------------------------ */

#define N_WORKERS   CONFIG_SHELL_ASYNC_WORKERS
#define STACK_SIZE  CONFIG_SHELL_ASYNC_STACK_SIZE
#define PRIO        CONFIG_SHELL_ASYNC_PRIORITY

#define MAX_ARGC    16
#define ARG_BUF_LEN 256

/* ------------------------------------------------------------------ */
/*  Per-slot state                                                       */
/* ------------------------------------------------------------------ */

struct async_job {
	const struct shell *sh;
	shell_async_fn      fn;
	int                 argc;
	char               *argv[MAX_ARGC];
	char                arg_buf[ARG_BUF_LEN];
};

struct async_slot {
	struct k_thread   thread;
	k_thread_stack_t *stack;        /* NULL = no stack allocated */
	struct async_job  job;
	atomic_t          busy;         /* 1 = running or pending cleanup */
	atomic_t          abort;
	struct k_work     cleanup_work; /* submitted by worker to free stack */
	int               id;
};

static struct async_slot slots[N_WORKERS];

/* ------------------------------------------------------------------ */
/*  System workqueue cleanup handler                                    */
/*  Runs in system workqueue context — NOT in the worker thread.        */
/* ------------------------------------------------------------------ */

static void cleanup_handler(struct k_work *work)
{
	struct async_slot *s = CONTAINER_OF(work, struct async_slot, cleanup_work);

	/*
	 * The worker thread has already returned from worker_entry() before
	 * submitting this work item, so k_thread_join() returns very quickly
	 * (the thread is in K_THREAD_DEAD or about to be).
	 */
	k_thread_join(&s->thread, K_FOREVER);

	/* Return the stack to the heap — safe now that thread is dead */
	k_thread_stack_free(s->stack);
	s->stack = NULL;

	/* Reset abort flag for next job */
	atomic_set(&s->abort, 0);

	/* Release the slot for the next submit() */
	atomic_set(&s->busy, 0);

	LOG_DBG("async#%d stack freed, slot available", s->id);
}

/* ------------------------------------------------------------------ */
/*  Worker thread                                                        */
/* ------------------------------------------------------------------ */

static void worker_entry(void *p1, void *p2, void *p3)
{
	struct async_slot *s = p1;

	atomic_set(&s->abort, 0);

	int ret = s->job.fn(s->job.sh, s->job.argc, s->job.argv);

	if (ret != 0 && !atomic_get(&s->abort)) {
		LOG_WRN("[async#%d] job finished with error %d", s->id, ret);
	}

	/*
	 * Submit cleanup to the system workqueue BEFORE returning.
	 * The workqueue handler will join this thread and free the stack.
	 * busy remains 1 until cleanup_handler() runs.
	 */
	k_work_submit(&s->cleanup_work);

	/* Thread returns here — stack is freed by cleanup_handler() shortly */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                           */
/* ------------------------------------------------------------------ */

int shell_async_submit(const struct shell *sh, size_t argc, char **argv,
		       shell_async_fn fn)
{
	struct async_slot *s = NULL;

	for (int i = 0; i < N_WORKERS; i++) {
		if (atomic_cas(&slots[i].busy, 0, 1)) {
			s = &slots[i];
			break;
		}
	}

	if (s == NULL) {
		shell_warn(sh, "[async] all %d workers busy", N_WORKERS);
		return -EBUSY;
	}

	/* Allocate a fresh stack from the heap */
	s->stack = k_thread_stack_alloc(STACK_SIZE, 0);
	if (s->stack == NULL) {
		atomic_set(&s->busy, 0);
		shell_error(sh, "[async#%d] stack alloc failed (heap full?)", s->id);
		return -ENOMEM;
	}

	/* Copy argv into the slot's private buffer */
	size_t n = MIN(argc, MAX_ARGC);
	size_t off = 0;

	for (size_t i = 0; i < n; i++) {
		size_t len = strlen(argv[i]) + 1;

		if (off + len > ARG_BUF_LEN) {
			k_thread_stack_free(s->stack);
			s->stack = NULL;
			atomic_set(&s->busy, 0);
			return -ENOMEM;
		}
		memcpy(&s->job.arg_buf[off], argv[i], len);
		s->job.argv[i] = &s->job.arg_buf[off];
		off += len;
	}

	s->job.sh   = sh;
	s->job.fn   = fn;
	s->job.argc = (int)n;

	/* Print BEFORE creating the thread to guarantee ordering:
	 * worker priority > shell priority, so the worker would preempt
	 * the shell immediately after k_thread_create() otherwise. */
	shell_print(sh, "[async#%d] job started (shell is free)", s->id);

	k_tid_t tid = k_thread_create(&s->thread, s->stack, STACK_SIZE,
				      worker_entry, s, NULL, NULL,
				      PRIO, 0, K_NO_WAIT);

	char name[16];

	snprintf(name, sizeof(name), "async_%d", s->id);
	k_thread_name_set(tid, name);

	return 0;
}

bool shell_async_busy(void)
{
	for (int i = 0; i < N_WORKERS; i++) {
		if (atomic_get(&slots[i].busy)) {
			return true;
		}
	}
	return false;
}

void shell_async_abort(void)
{
	for (int i = 0; i < N_WORKERS; i++) {
		if (atomic_get(&slots[i].busy)) {
			atomic_set(&slots[i].abort, 1);
		}
	}
}

bool shell_async_aborted(void)
{
	k_tid_t me = k_current_get();

	for (int i = 0; i < N_WORKERS; i++) {
		if (&slots[i].thread == (struct k_thread *)me) {
			return atomic_get(&slots[i].abort) != 0;
		}
	}
	return false;
}

int shell_async_worker_id(void)
{
	k_tid_t me = k_current_get();

	for (int i = 0; i < N_WORKERS; i++) {
		if (&slots[i].thread == (struct k_thread *)me) {
			return i;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/*  Init                                                                 */
/* ------------------------------------------------------------------ */

static int shell_async_init(void)
{
	for (int i = 0; i < N_WORKERS; i++) {
		slots[i].id    = i;
		slots[i].stack = NULL;
		atomic_set(&slots[i].busy,  0);
		atomic_set(&slots[i].abort, 0);
		k_work_init(&slots[i].cleanup_work, cleanup_handler);
	}
	return 0;
}

SYS_INIT(shell_async_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
