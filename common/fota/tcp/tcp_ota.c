/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* QCC74x FOTA TCP transport. No partition or flash implementation lives here.
 */
#include "../ota/ota_internal.h"
#include "qcc74x_fota.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

#ifdef CONFIG_PSRAM
#define TCP_RCVBUF     4096U
#define TCP_RECV_BLOCK 512U
#else
#define TCP_RCVBUF     1024U
#define TCP_RECV_BLOCK 128U
#endif

#define PROGRESS_STEP (64U * 1024U)

#define FOTA_TCP_STAGE_PREPARE 1U
#define FOTA_TCP_STAGE_HEADER  2U
#define FOTA_TCP_STAGE_RECV    3U
#define FOTA_TCP_STAGE_WRITE   4U
#define FOTA_TCP_STAGE_VERIFY  5U
#define FOTA_TCP_STAGE_DONE    6U
#define FOTA_TCP_STAGE_ERROR   7U

static uint8_t tcp_io[TCP_RCVBUF];
static atomic_t cancel_requested;
K_MUTEX_DEFINE(active_socket_lock);
static int active_socket = -1;

static bool fota_cancelled(void)
{
	return atomic_get(&cancel_requested) != 0;
}

static void fota_socket_set(int fd)
{
	k_mutex_lock(&active_socket_lock, K_FOREVER);
	active_socket = fd;
	k_mutex_unlock(&active_socket_lock);
}

static void fota_socket_clear(int fd)
{
	k_mutex_lock(&active_socket_lock, K_FOREVER);
	if (active_socket == fd) {
		active_socket = -1;
	}
	k_mutex_unlock(&active_socket_lock);
}

int qcc74x_fota_cancel(void)
{
	int fd;

	atomic_set(&cancel_requested, 1);

	/* Wake a worker blocked in recv() instead of waiting for SO_RCVTIMEO. */
	k_mutex_lock(&active_socket_lock, K_FOREVER);
	fd = active_socket;
	if (fd >= 0) {
		(void)zsock_shutdown(fd, ZSOCK_SHUT_RDWR);
	}
	k_mutex_unlock(&active_socket_lock);

	return 0;
}

int qcc74x_fota_start(const char *ip, uint16_t port)
{
	struct sockaddr_in server = {.sin_family = AF_INET};
	struct qcc74x_ota_header header = {0};
	struct qcc74x_ota_image image;
	struct timeval timeout = {.tv_sec = 10};
	uint32_t received, offset = 0, next_report;
	int64_t start_ms = k_uptime_get();
	int64_t phase_ms;
	int64_t recv_wait_ms = 0;
	int64_t write_ms = 0;
	int64_t hash_ms = 0;
	bool transfer_started = false;
	int rcvbuf = TCP_RCVBUF;
	int fd = -1, n, rc;

	atomic_set(&cancel_requested, 0);

	if (!ip || !port) {
		rc = -EINVAL;
		goto out;
	}

	rc = qcc74x_ota_prepare(&image);
	if (rc || fota_cancelled()) {
		if (fota_cancelled()) {
			rc = -ECANCELED;
		}
		goto out;
	}

	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		rc = -errno;
		goto out;
	}
	fota_socket_set(fd);

	if (zsock_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) ||
	    zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
		rc = -errno;
		goto out;
	}

	if (zsock_inet_pton(AF_INET, ip, &server.sin_addr) != 1) {
		rc = -EINVAL;
		goto out;
	}

	server.sin_port = htons(port);
	if (zsock_connect(fd, (struct sockaddr *)&server, sizeof(server))) {
		rc = fota_cancelled() ? -ECANCELED : -errno;
		goto out;
	}

	for (n = 0; n < QCC74X_OTA_HEADER_SIZE;) {
		if (fota_cancelled()) {
			rc = -ECANCELED;
			goto out;
		}
		rc = zsock_recv(fd, (uint8_t *)&header + n, QCC74X_OTA_HEADER_SIZE - n, 0);
		if (rc <= 0) {
			rc = fota_cancelled() ? -ECANCELED : (rc ? -errno : -ECONNRESET);
			goto out;
		}
		n += rc;
	}

	rc = qcc74x_ota_validate_header(&header, &image);

	if (rc) {
		goto out;
	}

	qcc74x_ota_stage_begin(&header);
	printk("OTA: downloading 0/%u bytes\n", header.length);
	transfer_started = true;

	/* Let the Wi-Fi/network threads queue the first immediate-stream payload. */
	k_sleep(K_MSEC(1));

	next_report = PROGRESS_STEP;

	while (offset < header.length) {
		if (fota_cancelled()) {
			rc = -ECANCELED;
			goto out;
		}
		phase_ms = k_uptime_get();
		rc = zsock_recv(fd, tcp_io, MIN(TCP_RECV_BLOCK, header.length - offset), 0);
		recv_wait_ms += k_uptime_get() - phase_ms;

		if (rc <= 0) {
			rc = fota_cancelled() ? -ECANCELED : (rc ? -errno : -ECONNRESET);
			goto out;
		}

		received = rc;
		phase_ms = k_uptime_get();
		rc = qcc74x_ota_write(&image, offset, tcp_io, received);
		write_ms += k_uptime_get() - phase_ms;

		if (rc) {
			goto out;
		}

		if (fota_cancelled()) {
			rc = -ECANCELED;
			goto out;
		}

		offset += received;

		if (offset >= next_report || offset == header.length) {
			printk("OTA: %u%% (%u/%u bytes)\n", offset * 100U / header.length, offset,
			       header.length);
			next_report = offset + PROGRESS_STEP;
		}
	}

	if (fota_cancelled()) {
		rc = -ECANCELED;
		goto out;
	}
	printk("OTA: verifying SHA-256\n");
	phase_ms = k_uptime_get();
	rc = qcc74x_ota_verify(&image, &header, tcp_io, sizeof(tcp_io));
	hash_ms = k_uptime_get() - phase_ms;

out:
	if (transfer_started) {
		printk("OTA stats: total=%u ms, recv wait time %lld ms, write=%lld ms, "
		       "SHA=%lld ms\n",
		       (uint32_t)(k_uptime_get() - start_ms), (long long)recv_wait_ms,
		       (long long)write_ms, (long long)hash_ms);
	}
	if (fd >= 0) {
		fota_socket_clear(fd);
	}
	if (fd >= 0) {
		zsock_close(fd);
	}

	qcc74x_ota_set_error(rc);
	return rc;
}
