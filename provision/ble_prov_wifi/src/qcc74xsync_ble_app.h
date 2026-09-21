/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74XSYNC_BLE_APP_H_
#define QCC74XSYNC_BLE_APP_H_

#include "transfer.h"

int qcc74xsync_ble_app_init(struct qcc74xsync_transfer *transfer);
int qcc74xsync_ble_app_event(void *ctx,
				     const struct qcc74xsync_event *event);

#endif /* QCC74XSYNC_BLE_APP_H_ */
