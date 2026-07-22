/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BSHG_TRACING_H
#define BSHG_TRACING_H
#include <zephyr/kernel.h>

void increase_tracingId(void);
void increase_seqId(void);
void set_seqId(uint32_t value);

extern void BSHG_initTracing(void);
extern void BSHG_startTracing(void);
extern void BSHG_stopTracing(void);
extern void BSHG_setTracepoint(const char *trcMsg);
void BSHG_setTracepoint_extended(const unsigned char *trcMsg, ...);
void BSHG_setTracepoint_detailed(uint32_t arg0, uint32_t arg1, const unsigned char *trcMsg, ...);

#if defined(CONFIG_TRACING_CTF)
void BSHG_ctfkeepalive(void);
#endif

#endif