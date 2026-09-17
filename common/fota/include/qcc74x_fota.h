/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74X_FOTA_H_
#define QCC74X_FOTA_H_

#include <stddef.h>
#include <stdint.h>

int qcc74x_fota_start(const char *ipv4, uint16_t port);
int qcc74x_fota_stage_file(const char *path);
int qcc74x_fota_cancel(void);
int qcc74x_ota_activate(void);
int qcc74x_ota_rollback(void);
void qcc74x_ota_status(char *buffer, size_t length);
int qcc74x_ota_slot(char *buffer, size_t length);
#endif
