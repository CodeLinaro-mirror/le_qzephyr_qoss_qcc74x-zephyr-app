/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#ifndef SPI_CLOSE_ROUNDTRIP_H
#define SPI_CLOSE_ROUNDTRIP_H

#include <zephyr/kernel.h>

// return 0 for success
int BSHG_spiTxRxShortcutFromHexstring(const char *data, int length);
int BSHG_spiTxRxShortcutFromBinarray(const uint8_t *data, int length);

void BSHG_spiSetMsgDataFromHexstring(const char *data, int length);
void BSHG_spiSetMsgDataFromBinArray(const uint8_t *data, int length);

#endif // SPI_CLOSE_ROUNDTRIP_H