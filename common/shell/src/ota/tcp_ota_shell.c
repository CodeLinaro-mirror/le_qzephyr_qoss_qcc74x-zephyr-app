/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ota_shell.h"

#include <errno.h>
#include <qcc74x_fota.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

/* OTA stays below all Wi-Fi worker threads (2..6), while shell stays at 0. */
#define OTA_THREAD_PRIORITY K_PRIO_PREEMPT(7)

#ifdef CONFIG_PSRAM
#define OTA_THREAD_STACK_SIZE 6144
#else
#define OTA_THREAD_STACK_SIZE 4096
#endif

#define OTA_IPV4_STRING_SIZE 16

K_THREAD_STACK_DEFINE(ota_tcp_thread_stack, OTA_THREAD_STACK_SIZE);

static struct k_thread ota_tcp_thread;
static char ota_tcp_ipv4[OTA_IPV4_STRING_SIZE];
static uint16_t ota_tcp_port;

static const char *ota_tcp_error_name(int error)
{
	switch (error) {
	case -ECANCELED:
		return "ECANCELED, cancelled by user";
	case -ETIMEDOUT:
		return "ETIMEDOUT, TCP receive timed out";
	case -ECONNRESET:
		return "ECONNRESET, peer closed or reset the connection";
	case -ECONNREFUSED:
		return "ECONNREFUSED, server rejected the connection";
	case -ENETUNREACH:
		return "ENETUNREACH, network is unreachable";
	case -EHOSTUNREACH:
		return "EHOSTUNREACH, host is unreachable";
	case -EIO:
		return "EIO, flash I/O failed";
	case -EINVAL:
		return "EINVAL, invalid OTA image or argument";
	case -EBADMSG:
		return "EBADMSG, SHA-256 verification failed";
	case -ENOMEM:
		return "ENOMEM, insufficient memory";
	case -EAGAIN:
		return "EAGAIN, resource temporarily unavailable; restart TCP server";
	default:
		return "unknown error";
	}
}

static void ota_tcp_worker(void *p1, void *p2, void *p3)
{
	int rc;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	rc = qcc74x_fota_start(ota_tcp_ipv4, ota_tcp_port);
	qcc74x_ota_shell_session_complete();

	if (rc) {
		printk("OTA failed (%d: %s)\n", rc, ota_tcp_error_name(rc));
	} else {
		printk("OTA staged (0)\n");
	}
}

static int cmd_ota_tcp_start(const struct shell *shell, size_t argc, char **argv)
{
	char *end;
	unsigned long port = 9000;
	size_t ipv4_length;
	int rc;

	if (argc == 3) {
		errno = 0;
		port = strtoul(argv[2], &end, 10);
		if (errno || *end || !port || port > UINT16_MAX) {
			shell_error(shell, "port must be 1..%u", UINT16_MAX);
			return -EINVAL;
		}
	}

	ipv4_length = strlen(argv[1]);
	if (!ipv4_length || ipv4_length >= sizeof(ota_tcp_ipv4)) {
		shell_error(shell, "invalid IPv4 address");
		return -EINVAL;
	}

	rc = qcc74x_ota_shell_session_begin("tcp", qcc74x_fota_cancel);
	if (rc) {
		shell_error(shell, "OTA is already running");
		return rc;
	}

	memcpy(ota_tcp_ipv4, argv[1], ipv4_length + 1);
	ota_tcp_port = (uint16_t)port;
	k_thread_create(&ota_tcp_thread, ota_tcp_thread_stack,
			K_THREAD_STACK_SIZEOF(ota_tcp_thread_stack), ota_tcp_worker, NULL, NULL,
			NULL, OTA_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ota_tcp_thread, "ota_tcp_worker");
	shell_print(shell, "TCP OTA started");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ota_tcp_commands,
			       SHELL_CMD_ARG(start, NULL, "start <IPv4> [port]", cmd_ota_tcp_start,
					     2, 1),
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((ota), tcp, &ota_tcp_commands, "TCP OTA transport", NULL, 1, 0);
