/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qcc74xsync_ble_app.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define SMARTCONFIG_WIFI_SCAN_MAX 50U
#define SMARTCONFIG_WIFI_PSK_MAX 64U

#define SMARTCONFIG_WIFI_EVENT_MASK \
	(NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | \
	 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

#define SCB_TRACE(fmt, ...) printk("scbtr|" fmt "\n", ##__VA_ARGS__)

struct qcc74xsync_wifi_scan_item {
	uint8_t ssid[WIFI_SSID_MAX_LEN + 1U];
	uint8_t ssid_length;
	uint8_t bssid[WIFI_MAC_ADDR_LEN];
	uint8_t channel;
	uint8_t auth;
	int8_t rssi;
};

static struct qcc74xsync_transfer *transfer;
static struct net_if *wifi_iface;
static struct net_mgmt_event_callback wifi_mgmt_callback;

static uint8_t wifi_ssid[WIFI_SSID_MAX_LEN + 1U];
static uint8_t wifi_ssid_length;
static uint8_t wifi_password[SMARTCONFIG_WIFI_PSK_MAX + 1U];
static uint8_t wifi_password_length;
static struct qcc74xsync_wifi_scan_item wifi_scan_items[SMARTCONFIG_WIFI_SCAN_MAX];
static uint8_t wifi_scan_read_index;
static uint8_t wifi_scan_write_index;
static bool wifi_scan_active;
K_MUTEX_DEFINE(wifi_cache_lock);

static uint8_t wifi_auth_from_security(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return 0U;
	case WIFI_SECURITY_TYPE_WEP:
	case WIFI_SECURITY_TYPE_WEP_OPEN:
	case WIFI_SECURITY_TYPE_WEP_SHARED:
		return 1U;
	case WIFI_SECURITY_TYPE_WPA_PSK:
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_PSK_SHA256:
	case WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL:
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO:
		return 2U;
	default:
		return 0xffU;
	}
}

static int json_escape(char *dst, size_t dst_size, const uint8_t *src,
			       size_t src_length)
{
	static const char hex[] = "0123456789abcdef";
	size_t used = 0U;

	if (dst == NULL || dst_size == 0U || (src == NULL && src_length != 0U)) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < src_length; i++) {
		uint8_t c = src[i];

		if (c == '"' || c == '\\') {
			if (used + 2U >= dst_size) {
				return -ENOSPC;
			}
			dst[used++] = '\\';
			dst[used++] = c;
		} else if (c < 0x20U) {
			if (used + 6U >= dst_size) {
				return -ENOSPC;
			}
			dst[used++] = '\\';
			dst[used++] = 'u';
			dst[used++] = '0';
			dst[used++] = '0';
			dst[used++] = hex[c >> 4];
			dst[used++] = hex[c & 0x0fU];
		} else {
			if (used + 1U >= dst_size) {
				return -ENOSPC;
			}
			dst[used++] = c;
		}
	}

	dst[used] = '\0';
	return 0;
}

static int wifi_set_credential(uint8_t *dst, uint8_t *dst_length,
			       size_t dst_size, const uint8_t *src, size_t src_length)
{
	if (dst == NULL || dst_length == NULL || src_length > dst_size ||
	    (src == NULL && src_length != 0U)) {
		return -EINVAL;
	}

	memset(dst, 0, dst_size);
	if (src_length != 0U) {
		memcpy(dst, src, src_length);
	}
	*dst_length = src_length;
	return 0;
}

static int wifi_start_scan(void)
{
	struct wifi_scan_params params = { 0 };
	int ret;

	if (wifi_iface == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&wifi_cache_lock, K_FOREVER);
	wifi_scan_read_index = 0U;
	wifi_scan_write_index = 0U;
	wifi_scan_active = true;
	memset(wifi_scan_items, 0, sizeof(wifi_scan_items));
	k_mutex_unlock(&wifi_cache_lock);

	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, &params, sizeof(params));
	SCB_TRACE("WIFI_SCAN_START|ret=%d|admin=%u", ret,
		  net_if_is_admin_up(wifi_iface) ? 1U : 0U);
	if (ret != 0) {
		k_mutex_lock(&wifi_cache_lock, K_FOREVER);
		wifi_scan_active = false;
		k_mutex_unlock(&wifi_cache_lock);
	}

	return ret;
}

static int wifi_connect(void)
{
	struct wifi_connect_req_params params = { 0 };

	if (wifi_iface == NULL || wifi_ssid_length == 0U) {
		return wifi_iface == NULL ? -ENODEV : -EINVAL;
	}

	params.ssid = wifi_ssid;
	params.ssid_length = wifi_ssid_length;
	params.psk = wifi_password;
	params.psk_length = wifi_password_length;
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.security = wifi_password_length == 0U ?
		WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL;
	params.mfp = wifi_password_length == 0U ? WIFI_MFP_DISABLE :
		WIFI_MFP_OPTIONAL;

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &params,
			       sizeof(params));
}

static int wifi_disconnect(void)
{
	if (wifi_iface == NULL) {
		return -ENODEV;
	}

	return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_iface, NULL, 0U);
}

static int wifi_format_scan_item(char *response, size_t response_size)
{
	struct qcc74xsync_wifi_scan_item item;
	char ssid[WIFI_SSID_MAX_LEN * 6U + 1U];
	int length;

	k_mutex_lock(&wifi_cache_lock, K_FOREVER);
	if (wifi_scan_write_index == 0U) {
		k_mutex_unlock(&wifi_cache_lock);
		return -EAGAIN;
	}
	if (wifi_scan_read_index == wifi_scan_write_index) {
		wifi_scan_read_index = 0U;
		wifi_scan_write_index = 0U;
		k_mutex_unlock(&wifi_cache_lock);
		return 0;
	}

	item = wifi_scan_items[wifi_scan_read_index++];
	SCB_TRACE("WIFI_ITEM|idx=%u|count=%u|chan=%u|auth=%u|rssi=%d|ssid_len=%u",
		  wifi_scan_read_index, wifi_scan_write_index, item.channel,
		  item.auth, item.rssi, item.ssid_length);
	k_mutex_unlock(&wifi_cache_lock);

	if (json_escape(ssid, sizeof(ssid), item.ssid, item.ssid_length) != 0) {
		return -ENOSPC;
	}

	length = snprintk(response, response_size,
			  "{\"chan\":%u,\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
			  "\"rssi\":%d,\"ssid\":\"%s\",\"auth\":%u}",
			  item.channel, item.bssid[0], item.bssid[1], item.bssid[2],
			  item.bssid[3], item.bssid[4], item.bssid[5], item.rssi,
			  ssid, item.auth);
	if (length < 0 || (size_t)length >= response_size) {
		return -ENOSPC;
	}

	SCB_TRACE("WIFI_JSON|len=%d", length);
	return length;
}

static int wifi_format_state(char *response, size_t response_size)
{
	struct wifi_iface_status status = { 0 };
	struct net_in_addr netmask;
	struct net_in_addr gateway;
	struct net_in_addr *address;
	char ssid[WIFI_SSID_MAX_LEN * 6U + 1U];
	char ip[NET_IPV4_ADDR_LEN] = "0.0.0.0";
	char mask[NET_IPV4_ADDR_LEN] = "0.0.0.0";
	char gw[NET_IPV4_ADDR_LEN] = "0.0.0.0";
	int state = 3;
	int length;

	if (wifi_iface == NULL || net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS,
					     wifi_iface, &status, sizeof(status)) != 0) {
		return wifi_iface == NULL ? -ENODEV : -EIO;
	}

	address = net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED);
	if (address != NULL) {
		netmask = net_if_ipv4_get_netmask_by_addr(wifi_iface, address);
		gateway = net_if_ipv4_get_gw(wifi_iface);
		(void)net_addr_ntop(NET_AF_INET, address, ip, sizeof(ip));
		(void)net_addr_ntop(NET_AF_INET, &netmask, mask, sizeof(mask));
		(void)net_addr_ntop(NET_AF_INET, &gateway, gw, sizeof(gw));
		state = 4;
	}

	if (json_escape(ssid, sizeof(ssid), (const uint8_t *)status.ssid,
				MIN((size_t)status.ssid_len,
				    (size_t)WIFI_SSID_MAX_LEN)) != 0) {
		return -ENOSPC;
	}

	length = snprintk(response, response_size,
			  "{\"state\":%d,\"ip\":\"%s\",\"gw\":\"%s\",\"mask\":\"%s\","
			  "\"ssid\":\"%s\",\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
			  state, ip, gw, mask, ssid, (uint8_t)status.bssid[0],
			  (uint8_t)status.bssid[1], (uint8_t)status.bssid[2],
			  (uint8_t)status.bssid[3], (uint8_t)status.bssid[4],
			  (uint8_t)status.bssid[5]);
	if (length < 0 || (size_t)length >= response_size) {
		return -ENOSPC;
	}

	return length;
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
					    uint64_t mgmt_event,
					    struct net_if *iface)
{
	const struct wifi_status *status;

	if (iface != wifi_iface) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *result = cb->info;
		struct qcc74xsync_wifi_scan_item *item;
		size_t ssid_length;

		if (result == NULL) {
			return;
		}
		k_mutex_lock(&wifi_cache_lock, K_FOREVER);
		if (!wifi_scan_active ||
		    wifi_scan_write_index >= SMARTCONFIG_WIFI_SCAN_MAX) {
			k_mutex_unlock(&wifi_cache_lock);
			return;
		}

		item = &wifi_scan_items[wifi_scan_write_index++];
		ssid_length = MIN((size_t)result->ssid_length,
				  (size_t)WIFI_SSID_MAX_LEN);
		memcpy(item->ssid, result->ssid, ssid_length);
		item->ssid[ssid_length] = '\0';
		item->ssid_length = ssid_length;
		memcpy(item->bssid, result->mac, WIFI_MAC_ADDR_LEN);
		item->channel = result->channel;
		item->auth = wifi_auth_from_security(result->security);
		item->rssi = result->rssi;
		SCB_TRACE("WIFI_ITEM_RX|idx=%u|chan=%u|auth=%u|rssi=%d|ssid_len=%u",
			  wifi_scan_write_index, item->channel, item->auth,
			  item->rssi, item->ssid_length);
		k_mutex_unlock(&wifi_cache_lock);
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		k_mutex_lock(&wifi_cache_lock, K_FOREVER);
		wifi_scan_active = false;
		k_mutex_unlock(&wifi_cache_lock);
		status = cb->info;
		SCB_TRACE("WIFI_SCAN_DONE|status=%d|count=%u",
			  status != NULL ? status->status : -EIO, wifi_scan_write_index);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		status = cb->info;
		printk("smartconfig_ble: Wi-Fi event 0x%llx result %d\n",
		       mgmt_event, status != NULL ? status->status : -EIO);
		break;
	default:
		break;
	}
}

int qcc74xsync_ble_app_init(struct qcc74xsync_transfer *new_transfer)
{
	transfer = new_transfer;
	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		printk("smartconfig_ble: Wi-Fi STA interface not available\n");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_mgmt_callback, wifi_mgmt_event_handler,
				     SMARTCONFIG_WIFI_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_mgmt_callback);
	return 0;
}

int qcc74xsync_ble_app_event(void *ctx, const struct qcc74xsync_event *event)
{
	char response[QCC74XSYNC_PROTOCOL_BUF_SIZE];
	int length;

	ARG_UNUSED(ctx);

	if (event->type == QCC74XSYNC_EVENT_DATA) {
		switch (event->id) {
		case QCC74XSYNC_DATA_ACK:
			return 0;
		case QCC74XSYNC_DATA_STA_WIFI_SSID:
			return wifi_set_credential(wifi_ssid, &wifi_ssid_length,
						   WIFI_SSID_MAX_LEN, event->data,
						   event->length);
		case QCC74XSYNC_DATA_STA_WIFI_PASSWORD:
			return wifi_set_credential(wifi_password, &wifi_password_length,
						   SMARTCONFIG_WIFI_PSK_MAX, event->data,
						   event->length);
		case QCC74XSYNC_DATA_STA_WIFI_BSSID:
			return event->length <= WIFI_MAC_ADDR_LEN ? 0 : -EINVAL;
		default:
			return -EINVAL;
		}
	}

	switch (event->id) {
	case QCC74XSYNC_CMD_ACK:
		return 0;
	case QCC74XSYNC_CMD_GET_VERSION:
		return qcc74xsync_transfer_set_response(transfer,
							QCC74XSYNC_PROTOCOL_VERSION,
							sizeof(QCC74XSYNC_PROTOCOL_VERSION));
	case QCC74XSYNC_CMD_CONNECT_WIFI:
		return wifi_connect();
	case QCC74XSYNC_CMD_DISCONNECT_WIFI:
		return wifi_disconnect();
	case QCC74XSYNC_CMD_GET_WIFI_STATUS:
	case QCC74XSYNC_CMD_WIFI_STATE_GET:
		length = wifi_format_state(response, sizeof(response));
		return length < 0 ? length :
			qcc74xsync_transfer_set_response(transfer, response, length);
	case QCC74XSYNC_CMD_WIFI_SCAN:
		return wifi_start_scan();
	case QCC74XSYNC_CMD_WIFI_DATA_GET:
		length = wifi_format_scan_item(response, sizeof(response));
		if (length == -EAGAIN) {
			return QCC74XSYNC_PROTOCOL_RESULT_NOT_READY;
		}
		return length < 0 ? length :
			qcc74xsync_transfer_set_response(transfer, response, length);
	default:
		return -EINVAL;
	}
}
