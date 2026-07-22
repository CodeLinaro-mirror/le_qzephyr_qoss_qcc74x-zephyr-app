/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#ifndef BSHG_THREADS_H
#define BSHG_THREADS_H

struct k_thread app_thread;
struct k_thread domain_thread;
struct k_thread ll_thread;
struct k_thread stress_thread;
#if defined(CONFIG_TRACING_CTF)
struct k_thread ctfkeepalive_thread;
#endif

void app_main(void *p1, void *p2, void *p3);
void domain_main(void *p1, void *p2, void *p3);
void ll_main(void *p1, void *p2, void *p3);

#endif // BSHG_THREADS_H