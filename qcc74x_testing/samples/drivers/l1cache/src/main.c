/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/cache.h>

#define DATA_SIZE (256 * 1024)
static uint8_t data[DATA_SIZE];

void calculate_sum(uint8_t *data, const char *str)
{
	uint64_t start, end;
	volatile int sum = 0;

	start = k_uptime_get();
	for (int i = 0; i < DATA_SIZE; i++) {
		sum += data[i];
	}
	end = k_uptime_get();

	printk("calculate sum %s time ms: %llu \n", str, end - start);
}

int main()
{

	printk("Cache test app started\n");

	printk("d-cache line size %d bytes\n", sys_cache_data_line_size_get());
	printk("i-cache line size %d bytes\n", sys_cache_instr_line_size_get());

	/* Fill test buffer */
	for (int i = 0; i < DATA_SIZE; i++) {
		data[i] = i;
	}

	printk("Show performance effect of cache operations\n");

	calculate_sum(data, "cache enabled");
	sys_cache_instr_disable();
	calculate_sum(data, "instr cache disabled");
	sys_cache_data_disable();
	calculate_sum(data, "data and instr cache disabled");
	sys_cache_instr_enable();
	sys_cache_data_enable();
	calculate_sum(data, "cache again enabled");

	/* Check if other l1 cache API options have expected return values */
	if (sys_cache_instr_flush_all() != -ENOTSUP) {
		printk("sys_cache_instr_flush_all should return ENOTSUP\n");
	}
	if (sys_cache_data_flush_all() != 0) {
		printk("sys_cache_data_flush_all failed\n");
	}
	if (sys_cache_instr_invd_all() != 0) {
		printk("sys_cache_data_invd_all failed\n");
	}
	if (sys_cache_data_invd_all() != 0) {
		printk("sys_cache_instr_invd_all failed\n");
	}
	if (sys_cache_instr_flush_and_invd_all() != -ENOTSUP) {
		printk("sys_cache_instr_flush_and_invd_all should return ENOTSUP\n");
	}
	if (sys_cache_data_flush_and_invd_all() != 0) {
		printk("sys_cache_data_flush_and_invd_all failed\n");
	}
	if (sys_cache_data_flush_range(data, 0xff) != 0) {
		printk("sys_cache_data_flush_range failed\n");
	}
	if (sys_cache_instr_flush_range(data, 0xff) != -ENOTSUP) {
		printk("sys_cache_instr_flush_range should return ENOTSUP\n");
	}
	if (sys_cache_data_invd_range(data, 0xff) != 0) {
		printk("sys_cache_data_invd_range failed\n");
	}
	if (sys_cache_instr_invd_range(data, 0xff) != -0) {
		printk("sys_cache_instr_invd_range failed\n");
	}
	if (sys_cache_data_flush_and_invd_range(data, 0xff) != 0) {
		printk("sys_cache_data_flush_and_invd_range failed\n");
	}
	if (sys_cache_instr_flush_and_invd_range(data, 0xff) != -ENOTSUP) {
		printk("cache_instr_flush_and_invd_range should return ENOTSUP\n");
	}

	printk("Cache test app finished\n");

	return 0;
}