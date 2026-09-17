/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74X_OTA_SHELL_H_
#define QCC74X_OTA_SHELL_H_

#include <stdbool.h>

typedef int (*qcc74x_ota_shell_cancel_fn_t)(void);

int qcc74x_ota_shell_session_begin(const char *transport, qcc74x_ota_shell_cancel_fn_t cancel);
void qcc74x_ota_shell_session_complete(void);
bool qcc74x_ota_shell_session_running(void);
const char *qcc74x_ota_shell_session_transport(void);
int qcc74x_ota_shell_session_cancel(void);

#endif /* QCC74X_OTA_SHELL_H_ */
