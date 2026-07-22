/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel/thread_stack.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>

#include <stress.h>
#include <BSHG_tracing.h>

#if defined(CONFIG_COUNTER_NATIVE_SIM)
#define HW_TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_MCUX_CTIMER)
#define HW_TIMER DT_NODELABEL(ctimer0)
#elif defined(CONFIG_COUNTER_TMR_QCC74X)
#define HW_TIMER DT_NODELABEL(timer0)
#endif

#define STRESS_TIMER_ID          0
#define STRESS_TIMER_BASE_US     100u
#define STRESS_CYCLES_1US_260MHZ 40u

struct counter_alarm_cfg timer_cfg;
static int stress_level = 0;
static uint32_t stress_time_us = 0;
static uint32_t stress_base_time_us = 100u;

int get_stress_level()
{
	return stress_level;
}

uint64_t get_stress_base_time_us()
{
	return stress_base_time_us;
}

static void timer_interrupt_handler(const struct device *counter_dev, uint8_t chan_id,
				    uint32_t ticks, void *user_data)
{
	struct counter_alarm_cfg *config = user_data;
	int err;

	config->ticks = counter_us_to_ticks(counter_dev, stress_base_time_us);

	if (stress_time_us > 0) {
		err = counter_set_channel_alarm(counter_dev, STRESS_TIMER_ID, user_data);
		if (err != 0) {
			printk("Stress HW timer restart error.\n");
		}
	}

	k_wakeup(&stress_thread);
}

void stress_main(void *p1, void *p2, void *p3)
{
	const struct device *const counter_dev = DEVICE_DT_GET(HW_TIMER);
	uint32_t wait_cycles, passed_cycles;

	if (!device_is_ready(counter_dev) || (counter_start(counter_dev) != 0)) {
		printk("Stress HW timer not ready.\n");
		return;
	}

	timer_cfg.flags = 0;
	timer_cfg.ticks = counter_us_to_ticks(counter_dev, STRESS_TIMER_BASE_US);
	timer_cfg.callback = timer_interrupt_handler;
	timer_cfg.user_data = &timer_cfg;

	k_sleep(K_FOREVER);

	while (1) {

		wait_cycles = stress_time_us * STRESS_CYCLES_1US_260MHZ;

		for (passed_cycles = 0u; passed_cycles < wait_cycles; passed_cycles++) {
			__asm__ volatile("nop");
		}

		k_sleep(K_FOREVER);
	}
}

void set_stress_level(int level)
{
	const struct device *const counter_dev = DEVICE_DT_GET(HW_TIMER);
	int err = 0;

	if (level == 0) {
		stress_level = 0;
		stress_time_us = 0u;
	} else {
		if (stress_time_us == 0) {
			err = counter_set_channel_alarm(counter_dev, STRESS_TIMER_ID, &timer_cfg);
		}

		if (err == 0) {
			if (level > 100) {
				level = 100;
			}
			stress_level = level;
			stress_time_us = (uint32_t)(stress_base_time_us * level / 100);
		} else {
			printk("Stress HW timer start error.\n");
		}
	}
}

void set_stress_base_time(uint64_t time_us)
{
	if (time_us >= STRESS_TIMER_BASE_US) {
		stress_base_time_us = (uint32_t)time_us;
	}
}
