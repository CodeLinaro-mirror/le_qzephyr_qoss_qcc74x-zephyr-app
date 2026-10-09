/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 * Not a contribution.
 */
#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/pwm.h>

#define DELAY 2000000
#define ALARM_CHANNEL_ID 0

struct counter_alarm_cfg alarm_cfg;

#define TIMER DT_NODELABEL(timer0)

static const struct pwm_dt_spec pwm0_spec = PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      uint8_t chan_id, uint32_t ticks,
				      void *user_data)
{
	struct counter_alarm_cfg *config = user_data;
	uint32_t now_ticks;
	uint64_t now_usec;
	int now_sec;
	int err;

	err = counter_get_value(counter_dev, &now_ticks);
	if (!counter_is_counting_up(counter_dev)) {
		now_ticks = counter_get_top_value(counter_dev) - now_ticks;
	}

	if (err) {
		printk("Failed to read counter value (err %d)", err);
		return;
	}

	now_usec = counter_ticks_to_us(counter_dev, now_ticks);
	now_sec = (int)(now_usec / USEC_PER_SEC);

	printk("!!! Alarm !!!\n");
	printk("Now: %u\n", now_sec);

	/* Set a new alarm with a double length duration */
	config->ticks = config->ticks * 2U;

	printk("Set alarm in %u sec (%u ticks)\n",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					   config->ticks) / USEC_PER_SEC),
	       config->ticks);

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
					user_data);
	if (err != 0) {
		printk("Alarm could not be set\n");
	}
}

void start_pwm(void)
{
	int ret;

	/* 2. Safety check: is the device ready? */
	if (!pwm_is_ready_dt(&pwm0_spec)) {
		printk("Error: PWM device %s is not ready\n", pwm0_spec.dev->name);
		return;
	}

	/* 3. Set the PWM: 1kHz frequency (1ms period), 50% duty cycle */
	/* Period and Pulse are in nanoseconds */
	ret = pwm_set_dt(&pwm0_spec, PWM_MSEC(1), PWM_MSEC(1) / 2);

	if (ret) {
		printk("Error %d: failed to set PWM\n", ret);
	} else {
		printk("PWM started on GPIO 2 (PWM0_CH2P)\n");
	}
}

int main(void)
{
	const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
	int err;

	printk("Counter alarm sample\n\n");

	if (!device_is_ready(counter_dev)) {
		printk("device not ready.\n");
		return 0;
	}

	start_pwm();

	counter_start(counter_dev);
	k_msleep(2);

	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY);
	alarm_cfg.callback = test_counter_interrupt_fn;
	alarm_cfg.user_data = &alarm_cfg;

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
					&alarm_cfg);
	printk("Set alarm in %u sec (%u ticks)\n",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					   alarm_cfg.ticks) / USEC_PER_SEC),
	       alarm_cfg.ticks);

	if (-EINVAL == err) {
		printk("Alarm settings invalid\n");
	} else if (-ENOTSUP == err) {
		printk("Alarm setting request not supported\n");
	} else if (err != 0) {
		printk("Error\n");
	}

	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
