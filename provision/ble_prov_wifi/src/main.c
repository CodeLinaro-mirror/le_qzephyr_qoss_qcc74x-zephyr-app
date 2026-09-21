/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>

#include "qcc74xsync_ble.h"

int main(void)
{
	return qcc74xsync_ble_start();
}
