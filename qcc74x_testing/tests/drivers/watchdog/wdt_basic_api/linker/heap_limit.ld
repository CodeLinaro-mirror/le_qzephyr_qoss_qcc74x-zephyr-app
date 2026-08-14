/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cap the wifi heap so it cannot grow into WDT_NOINIT.
 * ADDR(wifi_bss) + SIZEOF(wifi_bss) = _heap_wifi_start (actual heap start,
 * right after BSS), so BSS space is properly excluded from the heap size.
 * This fragment must be injected AFTER wifi_bss is defined (NOINIT_SECTIONS
 * slot) so that ADDR() and SIZEOF() resolve correctly.
 */
	_heap_wifi_size = ORIGIN(WDT_NOINIT) - ADDR(wifi_bss) - SIZEOF(wifi_bss);
