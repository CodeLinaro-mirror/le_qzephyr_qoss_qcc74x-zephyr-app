/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <tracing_backend.h>

#include <BSHG_tracing.h>

#ifdef CONFIG_PERCEPIO_TRACERECORDER
static TraceStringHandle_t BSHG_traceHandle;
#endif

uint32_t BSHG_tracingId = 0u;
uint32_t BSHG_seqId = 0u;

void increase_seqId(void)
{
	BSHG_seqId++;
}

void set_seqId(uint32_t value)
{
	BSHG_seqId = value;
}

void increase_tracingId(void)
{
	BSHG_tracingId++;
	set_seqId(0);
}

void BSHG_initTracing(void)
{
#if defined CONFIG_PERCEPIO_TRACERECORDER
	if (xTraceStringRegister("USR:", &BSHG_traceHandle) != TRC_SUCCESS) {
		printk("trc user-events not ready.\n");
	}
#elif defined CONFIG_SEGGER_SYSTEMVIEW
	SEGGER_SYSVIEW_Conf();
	uint8_t i;
	for (i = 1u; i <= 32; i++) {
		SEGGER_SYSVIEW_DisableEvents(i);
	}
#endif
}

void BSHG_startTracing(void)
{
#ifdef CONFIG_SEGGER_SYSTEMVIEW
	SEGGER_SYSVIEW_Start();
#endif

#if defined(CONFIG_TRACING_BACKEND_RAM)
	struct tracing_backend *working_backend = tracing_backend_get("tracing_backend_ram");
	tracing_backend_init(working_backend);
#endif
}

void BSHG_stopTracing(void)
{
#ifdef CONFIG_SEGGER_SYSTEMVIEW
	SEGGER_SYSVIEW_Stop();
#endif
}

void BSHG_setTracepoint_detailed(uint32_t arg0, uint32_t arg1, const unsigned char *trcMsg, ...)
{
	va_list args;
	va_start(args, trcMsg);
	unsigned char message[25] = {0};
	vsnprintf((char *)message, sizeof(message), trcMsg, args);
	va_end(args);
	sys_trace_named_event(message, arg0, arg1);
}

void BSHG_setTracepoint_extended(const unsigned char *trcMsg, ...)
{
	va_list args;
	va_start(args, trcMsg);
	unsigned char message[25] = {0};
	vsnprintf((char *)message, sizeof(message), trcMsg, args);
	va_end(args);
	BSHG_setTracepoint(message);
}

void BSHG_setTracepoint(const char *trcMsg)
{
#if defined(CONFIG_PERCEPIO_TRACERECORDER)
	xTracePrint(BSHG_traceHandle, trcMsg);
#elif defined(CONFIG_SEGGER_SYSTEMVIEW)
	sys_trace_named_event(trcMsg, BSHG_tracingId, BSHG_seqId);
	increase_seqId();
#elif defined(CONFIG_TRACING_CTF)
	sys_trace_named_event(trcMsg, BSHG_tracingId, BSHG_seqId);
	increase_seqId();
#endif
}

// create a thread which logs a CTF keepalive every 3.5 seconds
#if defined(CONFIG_TRACING_CTF)
void BSHG_ctfkeepalive(void)
{
	sys_trace_named_event("ctf_keepalive", 0, 0);
}
#endif
