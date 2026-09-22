/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

	wdt_noinit (NOLOAD) :
	{
		*(.wdt_noinit*)
	} > WDT_NOINIT
