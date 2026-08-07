/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/ir.h>

int main()
{
	static const struct device *ir = DEVICE_DT_GET(DT_NODELABEL(ir));
	uint64_t data;

	if (!ir) {
		printk("Cannot access IR device\n");
		return -1;
	}

	if (!device_is_ready(ir)) {
		printk("IR device not ready\n");
		return -1;
	}

	printk("Press any button in NEC remote contoller \n");
	k_msleep(100);

	ir_receive(ir, &data);
	printk("IR data received: x%016llx\n", data);

	return 0;
}
