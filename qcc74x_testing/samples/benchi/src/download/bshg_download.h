/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */


#ifndef BSHG_DOWNLOAD_H
#define BSHG_DOWNLOAD_H

#include <jsmn-extended.h>

int bshg_start_download(const jsmn_json_t *url, const jsmn_json_t *dst);

#endif // BSHG_DOWNLOAD_H