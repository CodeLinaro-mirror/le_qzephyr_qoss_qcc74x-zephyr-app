/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <jsmn-extended.h>

#include <bshg/filesystem.h>

#include <BSHG_tracing.h>

LOG_MODULE_REGISTER(http_download, LOG_LEVEL_DBG);

#define DOWNLOAD_URL "http://yourserver.com/firmware.bin"

uint32_t bshg_download_to_dst_flash_offset = 0;

#ifdef CONFIG_BSHG_IFP_PARALLEL_FLASHING
static void rx_flashdata_main(void *p1, void *p2, void *p3);
K_THREAD_DEFINE(rx_flashdata, 16384, rx_flashdata_main, NULL, NULL, NULL, 4, 0, 0);

typedef struct bshg_dl_queue_elem_t {
	unsigned char data[1024];
	size_t length;
} bshg_dl_queue_elem_t;

K_MSGQ_DEFINE(download_data_queue, sizeof(bshg_dl_queue_elem_t), 10, 1);
#endif

static void bshg_download_to_dst_flash(struct http_response *rsp, enum http_final_call final_data,
				       void *user_data)
{
	size_t bytes_received = rsp->body_frag_len;
	LOG_DBG("Received %d bytes", bytes_received);

	jsmn_json_t data = {.data = rsp->body_frag_start, .length = bytes_received};

	if (benchi_flash_raw_write(&data, bshg_download_to_dst_flash_offset) >= 0) {
		LOG_DBG("Successfully written %d bytes to flash", bytes_received);
		bshg_download_to_dst_flash_offset += data.length;
	} else {
		LOG_ERR("Failed to write chunk to flash");
		return;
	}

	if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("Download complete");
	}
}

static void bshg_download_to_dst_flash_ifp(struct http_response *rsp,
					   enum http_final_call final_data, void *user_data)
{
	size_t bytes_received = rsp->body_frag_len;
	LOG_DBG("Received %d bytes", bytes_received);

	BSHG_setTracepoint_extended("start_flashing");

#ifdef CONFIG_BSHG_IFP_PARALLEL_FLASHING
	bshg_dl_queue_elem_t data = {.length = bytes_received};
	memcpy(data.data, rsp->body_frag_start, sizeof(data.data));

	if (k_msgq_put(&download_data_queue, &data, K_SECONDS(10)) < 0) {
		BSHG_setTracepoint_extended("download_queue_error");
		return;
	}
#else
	jsmn_json_t data = {.data = rsp->body_frag_start, .length = bytes_received};

	if (benchi_flash_raw_write_ifp(&data, bshg_download_to_dst_flash_offset) >= 0) {
		LOG_DBG("Successfully written n bytes to flash ifp");
		bshg_download_to_dst_flash_offset += data.length;
	} else {
		LOG_ERR("Failed to write chunk to flash ifp");
		return;
	}
#endif
}

#ifdef CONFIG_BSHG_IFP_PARALLEL_FLASHING
static void rx_flashdata_main(void *p1, void *p2, void *p3)
{
	bshg_dl_queue_elem_t queue_data;

	while (1) {
		if (k_msgq_get(&download_data_queue, &queue_data, K_FOREVER) < 0) {
			return; // Failed to get download data
		}

		jsmn_json_t data = {.data = queue_data.data, .length = queue_data.length};

		if (benchi_flash_raw_write_ifp(&data, bshg_download_to_dst_flash_offset) >= 0) {
			LOG_DBG("Successfully written n bytes to flash ifp");
			bshg_download_to_dst_flash_offset += data.length;
		} else {
			LOG_ERR("Failed to write chunk to flash ifp");
			return;
		}
	}
}
#endif

static void bshg_download_to_dst_ram(struct http_response *rsp, enum http_final_call final_data,
				     void *user_data)
{
	// TBD
}

static void bshg_download_to_dst_littlefs(struct http_response *rsp,
					  enum http_final_call final_data, void *user_data)
{
	size_t bytes_received = rsp->body_frag_len;
	LOG_DBG("Received %d bytes", bytes_received);

	const jsmn_property_t path = {.data = "/lfs/download.bin",
				      .length = strlen("/lfs/download.bin")};

	// Write chunks to littlefs filesystem
	uint16_t bytes_written = 0;

	jsmn_json_t data = {.data = rsp->body_frag_start, .length = bytes_received};

	// Write the data to the file
	bytes_written = append_file(&path, &data);
	if (bytes_written >= 0) {
		LOG_DBG("Successfully written %d bytes to %s", bytes_written, path.data);
	} else {
		LOG_ERR("Failed to write chunk to file");
	}

	if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("Download complete");
	}
}

static void bshg_download_to_dst_discard(struct http_response *rsp, enum http_final_call final_data,
					 void *user_data)
{
	// just print data
	size_t bytes_received = rsp->body_frag_len;
	LOG_DBG("Received %d bytes", bytes_received);

	if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("Download complete");
	}
}

int bshg_start_download(const jsmn_json_t *url, const jsmn_json_t *dst)
{
	LOG_INF("Preparing download ...");
	void *response_cb = NULL;
	if (strncmp(dst->data, "flashifp", 8) == 0) {
		bshg_download_to_dst_flash_offset = 0;
		response_cb = bshg_download_to_dst_flash_ifp;
	} else if (strncmp(dst->data, "flash", 5) == 0) {
		bshg_download_to_dst_flash_offset = 0;
		response_cb = bshg_download_to_dst_flash;
	} else if (strncmp(dst->data, "ram", 3) == 0) {
		response_cb = bshg_download_to_dst_ram;
	} else if (strncmp(dst->data, "littlefs", 8) == 0) {
		response_cb = bshg_download_to_dst_littlefs;
		jsmn_json_t path = {.data = "/lfs/download.bin"};
		path.length = strlen(path.data);
		remove_file(&path);
	} else if (strncmp(dst->data, "discard", 7) == 0) {
		response_cb = bshg_download_to_dst_discard;
	} else {
		LOG_ERR("Unknown destination: %s", dst->data);
		return -1;
	}

	char url_null_terminated[256] = {0};
	strlcpy(url_null_terminated, url->data, sizeof(url_null_terminated));

	// Separate the protocol (http://) from the rest of the URL
	char *host = NULL;
	char *path = NULL;
	char path_str[256] = {0};

	// move host pointer to correct position at url_null_terminated
	if (strncmp(url_null_terminated, "http://", 7) == 0) {
		host = url_null_terminated + 7; // Skip "http://"
	} else if (strncmp(url_null_terminated, "https://", 8) == 0) {
		host = url_null_terminated + 8; // Skip "https://"
	} else {
		LOG_ERR("Invalid URL: %s", url_null_terminated);
		return -1;
	}

	// Find the first '/' after the host and null-terminate host string
	path = strchr(host, '/');
	if (path != NULL) {
		// copy the path to path_str
		strlcpy(path_str, path, sizeof(path_str));

		// Null-terminate the host string at the first '/'
		*path = '\0';
	} else {
		// If no '/', the whole URL is the host
		strlcpy(path_str, "/", sizeof(path_str));
	}

	path = path_str;

	// Now, 'host' contains the domain and 'path' contains the rest of the URL
	LOG_INF("Host: '%s', Path: '%s'", host, path);

	int ret = 0;
#if defined(CONFIG_ZTEST)
	return 0;
#else
	// Resolve the host to an IP address
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	struct addrinfo *res;
	ret = getaddrinfo(host, "80", &hints, &res);
	if (ret != 0) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -1;
	}

	// Create the socket
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Socket error");
		return -1;
	}

	// Connect the socket to the server
	ret = connect(sock, res->ai_addr, res->ai_addrlen);
	if (ret < 0) {
		LOG_ERR("Connect failed: %d", errno);
		close(sock);
		freeaddrinfo(res);
		return -1;
	}

	// Set up http request
	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = response_cb,
		.recv_buf = k_malloc(1024),
		.recv_buf_len = 1024,
	};

	// Send the request
	LOG_INF("Starting download ...");
	ret = http_client_req(sock, &req, 30000, NULL);
	if (ret < 0) {
		LOG_ERR("HTTP request failed: %d", ret);
	}

	close(sock);
	k_free(req.recv_buf);
#endif
	return ret;
}
