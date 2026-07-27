/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

static void trigger_callback(const struct device *dev, void *user_data)
{
	printk("%s \n", (const char *)(user_data));
}

#define GPIO_OUT_NODE DT_ALIAS(led0)
#define GPIO_IN_NODE  DT_ALIAS(sw0)

static const struct gpio_dt_spec gpio_out = GPIO_DT_SPEC_GET(GPIO_OUT_NODE, gpios);
static const struct gpio_dt_spec gpio_in = GPIO_DT_SPEC_GET(GPIO_IN_NODE, gpios);

static const struct device *comparator0 = DEVICE_DT_GET(DT_NODELABEL(comparator0));
static const struct device *comparator1 = DEVICE_DT_GET(DT_NODELABEL(comparator1));

int main()
{
	const int32_t SHORT_SLEEP_TIME_MS = 250;

	if (!comparator0 || !comparator1) {
		printk("Cannot access comparator device");
		return -1;
	}

	if (!device_is_ready(comparator0) || !device_is_ready(comparator1)) {
		printk("Comparator device not ready\n");
		return -1;
	}

	if (!(gpio_is_ready_dt(&gpio_in))) {
		printk("Error: gpio_in device %s is not ready\n", gpio_in.port->name);
		return -1;
	}

	if (!(gpio_is_ready_dt(&gpio_out))) {
		printk("Error: gpio_out device %s is not ready\n", gpio_in.port->name);
		return -1;
	}

	if (gpio_pin_configure_dt(&gpio_out, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("Error: failed to configure %s pin %d\n", gpio_out.port->name, gpio_out.pin);
		return -1;
	}

	if (gpio_pin_configure_dt(&gpio_in, GPIO_INPUT) != 0) {
		printk("Error: failed to configure %s pin %d\n", gpio_in.port->name, gpio_in.pin);
		return -1;
	}

	/** Comparator test **/
	printk("Analog comparator demo test app. Please make loopback connection between gpio20 "
	       "and gpio02 to make it work \n");

	/*Pin output is 0, so comparator output should be 0 */
	int val = gpio_pin_get_dt(&gpio_in);
	printk("gpio_in state, epxected 0, actual=%d \n", val);
	printk("Comparator state, excpected 0, actual=%d \n", comparator_get_output(comparator0));

	/* Pin output is 1, so comparator output should be 1 */
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	val = gpio_pin_get_dt(&gpio_in);
	printk("gpio_in state, expected 1, actual=%d \n", val);
	printk("Comparator state, expected 1, actual=%d \n", comparator_get_output(comparator0));

	/* Comparator moved from 1 to 0, so falling edge should cause the trigger  */
	comparator_set_trigger(comparator0, COMPARATOR_TRIGGER_FALLING_EDGE);
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 1, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));
	/* Calling comparator_trigger_is_pending should clear pending state */
	printk("Trigger is pending, expected 0, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));

	/* Comparator moved from 0 to 1, so falling edge should not cause the trigger */
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 0, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));

	/* Trigger edge is now rising, so 1->0 should not trigger, but 0->1 should */
	comparator_set_trigger(comparator0, COMPARATOR_TRIGGER_RISING_EDGE);
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 0, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 1, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));

	/* With both edges, all transitions should cause trigger */
	comparator_set_trigger(comparator0, COMPARATOR_TRIGGER_BOTH_EDGES);
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 1, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	printk("Trigger is pending, expected 1, actual=%d \n",
	       comparator_trigger_is_pending(comparator0));

	/* Check if callback is executed if triggerd */
	char *data = "I am comparator 0";
	comparator_set_trigger_callback(comparator0, trigger_callback, data);
	printk("Next expected message is \"%s\" \n", data);
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);

	/* Test both comparators.  */
	char *data1 = "I am comparator 1";
	comparator_set_trigger(comparator1, COMPARATOR_TRIGGER_BOTH_EDGES);
	comparator_set_trigger_callback(comparator1, trigger_callback, data1);
	printk("Expect callback messages from both comparators \n");
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);

	/* Comparator instances should recognize if they own the trigger. */
	comparator_set_trigger(comparator0, COMPARATOR_TRIGGER_FALLING_EDGE);
	comparator_set_trigger(comparator1, COMPARATOR_TRIGGER_RISING_EDGE);
	printk("Expect callback message from comparator0 \n");
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);

	printk("Expect callback message from comparator1 \n");
	gpio_pin_toggle_dt(&gpio_out);
	k_msleep(SHORT_SLEEP_TIME_MS);
	return 0;
}