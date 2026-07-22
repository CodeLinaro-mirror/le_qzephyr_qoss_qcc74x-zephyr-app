/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef STRESS_H
#define STRESS_H
#include <zephyr/kernel/thread_stack.h>
#include <zephyr/kernel.h>

#define STACK_SIZE_STRESS  4096
#define PRIO_STRESS_THREAD 4

extern struct k_thread stress_thread;

int get_stress_level();
uint64_t get_stress_base_time_us();

void stress_main(void *p1, void *p2, void *p3);
void set_stress_level(int level);
void set_stress_base_time(uint64_t time_us);

#endif // STRESS_H
