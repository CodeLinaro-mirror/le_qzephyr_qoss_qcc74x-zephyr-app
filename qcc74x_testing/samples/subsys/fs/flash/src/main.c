/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>

int main(void)
{
	extern int test_littlefs(void);
	test_littlefs();

	extern int test_raw(void);
	test_raw();

	return 0;
}
