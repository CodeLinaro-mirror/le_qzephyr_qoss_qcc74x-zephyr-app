/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <bshg_threads.h>
#include <bshg_zbus_helper.h>

#include <zephyr/logging/log.h>

#include <stress.h>

#if defined(CONFIG_TRACING_CTF)
#include <BSHG_tracing.h>
#endif

LOG_MODULE_REGISTER(threads);

#define STACK_SIZE 4 * 4096 // Stack size for each thread
#define PRIORITY_5 5        // Thread priority
#define PRIORITY_1 1        // Thread priority

K_THREAD_STACK_DEFINE(app_thread_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(domain_thread_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(ll_thread_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(stress_thread_stack, STACK_SIZE_STRESS);
#if defined(CONFIG_TRACING_CTF)
K_THREAD_STACK_DEFINE(ctfkeepalive_thread_stack, STACK_SIZE);
#endif

#if defined(CONFIG_TRACING_CTF)
// CTF keepalive thread to make a log entry every 3.5 seconds
void BSHG_ctfkeepalive_main(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sleep(K_MSEC(3500));
		BSHG_ctfkeepalive();
	}
}
#endif

void create_threads(void)
{
	k_thread_create(&app_thread, app_thread_stack, STACK_SIZE, app_main, NULL, NULL, NULL,
			PRIORITY_5, 0, K_NO_WAIT);
	k_thread_name_set(&app_thread, "app");

	k_thread_create(&domain_thread, domain_thread_stack, STACK_SIZE, domain_main, NULL, NULL,
			NULL, PRIORITY_5, 0, K_NO_WAIT);
	k_thread_name_set(&domain_thread, "domain");

	k_thread_create(&ll_thread, ll_thread_stack, STACK_SIZE, ll_main, NULL, NULL, NULL,
			PRIORITY_5, 0, K_NO_WAIT);
	k_thread_name_set(&ll_thread, "ll");

	k_thread_create(&stress_thread, stress_thread_stack, STACK_SIZE_STRESS, stress_main, NULL,
			NULL, NULL, PRIO_STRESS_THREAD, 0, K_NO_WAIT);
	k_thread_name_set(&stress_thread, "stress");

#if defined(CONFIG_TRACING_CTF)
	k_thread_create(&ctfkeepalive_thread, ctfkeepalive_thread_stack, STACK_SIZE,
			BSHG_ctfkeepalive_main, NULL, NULL, NULL, PRIORITY_1, 0, K_NO_WAIT);
	k_thread_name_set(&ll_thread, "ll");
#endif
}
