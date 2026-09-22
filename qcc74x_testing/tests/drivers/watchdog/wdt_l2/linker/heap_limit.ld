/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Keep the Wi-Fi heap below WDT_NOINIT, matching the L0 watchdog test.
 */
	_heap_wifi_size = ORIGIN(WDT_NOINIT) - ADDR(wifi_bss) - SIZEOF(wifi_bss);
