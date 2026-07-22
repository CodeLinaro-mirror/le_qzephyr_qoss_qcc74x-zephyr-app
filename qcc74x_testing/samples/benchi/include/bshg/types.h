/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#ifndef BSHG_TYPES_H
#define BSHG_TYPES_H

enum enum_bshg_result {
	E_SUCCESS = 0,
	E_TIMEOUT,
	E_ERROR,
};
typedef enum enum_bshg_result result_t;

typedef char json_msg[500];

#endif // BSHG_TYPES_H