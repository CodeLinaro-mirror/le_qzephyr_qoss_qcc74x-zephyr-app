/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qcc74xsync_ble.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "qcc74xsync_ble_app.h"

#define SMARTCONFIG_SERVICE_UUID 0xFFFF
#define SMARTCONFIG_WRITE_UUID 0xFF01
#define SMARTCONFIG_READ_UUID 0xFF02
#define SMARTCONFIG_RX_QUEUE_SIZE 2
#define SMARTCONFIG_PROTOCOL_REVISION "rx-v6"

#define SCB_TRACE(fmt, ...) printk("scbtr|" fmt "\n", ##__VA_ARGS__)

struct smartconfig_rx_packet {
	uint16_t length;
	uint8_t data[QCC74XSYNC_PROTOCOL_BUF_SIZE];
};

static struct bt_uuid_16 smartconfig_service_uuid =
	BT_UUID_INIT_16(SMARTCONFIG_SERVICE_UUID);
static struct bt_uuid_16 smartconfig_write_uuid =
	BT_UUID_INIT_16(SMARTCONFIG_WRITE_UUID);
static struct bt_uuid_16 smartconfig_read_uuid =
	BT_UUID_INIT_16(SMARTCONFIG_READ_UUID);

static struct qcc74xsync_transfer transfer;
static struct bt_conn *current_conn;
static struct bt_gatt_exchange_params exchange_params;

static uint8_t read_value[QCC74XSYNC_PROTOCOL_BUF_SIZE];
static uint16_t read_value_length;
static bool read_waiting_for_ack;
static bool notify_enabled;

K_MSGQ_DEFINE(smartconfig_rx_queue, sizeof(struct smartconfig_rx_packet),
		      SMARTCONFIG_RX_QUEUE_SIZE, 4);
K_THREAD_STACK_DEFINE(smartconfig_thread_stack, 4096);
static struct k_thread smartconfig_thread_data;

static ssize_t smartconfig_read(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset);
static ssize_t smartconfig_write(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, const void *buf,
				 uint16_t len, uint16_t offset, uint8_t flags);
static void smartconfig_ccc_changed(const struct bt_gatt_attr *attr,
					    uint16_t value);

BT_GATT_SERVICE_DEFINE(smartconfig_service,
	BT_GATT_PRIMARY_SERVICE(&smartconfig_service_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&smartconfig_read_uuid.uuid,
				       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
				       BT_GATT_PERM_READ, smartconfig_read, NULL, NULL),
	BT_GATT_CCC(smartconfig_ccc_changed,
			    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&smartconfig_write_uuid.uuid,
				       BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				       BT_GATT_PERM_WRITE, NULL, smartconfig_write, NULL)
);

static int qcc74xsync_ble_send(void *ctx, const uint8_t *data, size_t length)
{
	int err = 0;

	ARG_UNUSED(ctx);

	if (current_conn == NULL || length > sizeof(read_value)) {
		return -ENOTCONN;
	}

	memcpy(read_value, data, length);
	read_value_length = length;
	read_waiting_for_ack = !notify_enabled;
	SCB_TRACE("GATT_TX|len=%u|notify=%u", (unsigned int)length,
		  notify_enabled ? 1U : 0U);

	if (notify_enabled) {
		err = bt_gatt_notify(current_conn, &smartconfig_service.attrs[2],
				     data, length);
		if (err != 0) {
			return err;
		}
		return qcc74xsync_transfer_ack(&transfer);
	}

	return 0;
}

static ssize_t smartconfig_read(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	uint8_t value[QCC74XSYNC_PROTOCOL_BUF_SIZE];
	uint16_t value_length;
	ssize_t ret;

	if (read_value_length == 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_PDU);
	}

	value_length = read_value_length;
	memcpy(value, read_value, value_length);
	read_value_length = 0U;
	if (read_waiting_for_ack) {
		read_waiting_for_ack = false;
		SCB_TRACE("GATT_ACK|source=read");
		(void)qcc74xsync_transfer_ack(&transfer);
	}
	SCB_TRACE("GATT_RD|offset=%u|request=%u|value=%u", offset, len,
		  value_length);
	ret = bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				value_length);
	return ret;
}

static ssize_t smartconfig_write(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, const void *buf,
				 uint16_t len, uint16_t offset, uint8_t flags)
{
	struct smartconfig_rx_packet packet;

	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U || len > sizeof(packet.data)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (current_conn != conn) {
		if (current_conn != NULL) {
			bt_conn_unref(current_conn);
		}
		current_conn = bt_conn_ref(conn);
	}

	packet.length = len;
	memcpy(packet.data, buf, len);
	SCB_TRACE("GATT_WR|len=%u", len);
	if (k_msgq_put(&smartconfig_rx_queue, &packet, K_NO_WAIT) != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	return len;
}

static void smartconfig_ccc_changed(const struct bt_gatt_attr *attr,
					    uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = value == BT_GATT_CCC_NOTIFY;
	printk("smartconfig_ble: notifications %s\n",
	       notify_enabled ? "enabled" : "disabled");
}

static void exchange_complete(struct bt_conn *conn, uint8_t err,
				      struct bt_gatt_exchange_params *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);
	printk("smartconfig_ble: MTU exchange %s\n",
	       err == 0U ? "successful" : "failed");
}

static int qcc74xsync_ble_start_advertising(void);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		printk("smartconfig_ble: connection failed (%u)\n", err);
		return;
	}

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
	}
	current_conn = bt_conn_ref(conn);
	exchange_params.func = exchange_complete;
	(void)bt_gatt_exchange_mtu(conn, &exchange_params);
	printk("smartconfig_ble: connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (current_conn == conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	read_value_length = 0U;
	read_waiting_for_ack = false;
	qcc74xsync_transfer_reset(&transfer);
	printk("smartconfig_ble: disconnected (%u)\n", reason);
	if (qcc74xsync_ble_start_advertising() == 0) {
		printk("smartconfig_ble: advertising restarted\n");
	}
}

static struct bt_conn_cb connection_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static const uint8_t manufacturer_data[] = "QCC743";
static const uint8_t device_name[] = "BTBLE-DEV";
static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
};
static const struct bt_data scan_response_data[] = {
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manufacturer_data,
		 sizeof(manufacturer_data) - 1U),
	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, sizeof(device_name) - 1U),
};
static const struct bt_le_adv_param advertising_param = {
	.options = BT_GAP_ADV_PROP_CONNECTABLE | BT_LE_ADV_OPT_SCANNABLE,
	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
};

static int qcc74xsync_ble_start_advertising(void)
{
	return bt_le_adv_start(&advertising_param, advertising_data,
				       ARRAY_SIZE(advertising_data), scan_response_data,
				       ARRAY_SIZE(scan_response_data));
}

static void bluetooth_ready(int err)
{
	if (err != 0) {
		printk("smartconfig_ble: Bluetooth init failed (%d)\n", err);
		return;
	}

	bt_conn_cb_register(&connection_callbacks);
	err = qcc74xsync_ble_start_advertising();
	if (err != 0) {
		printk("smartconfig_ble: advertising failed (%d)\n", err);
		return;
	}

	printk("smartconfig_ble: advertising as BTBLE-DEV\n");
	printk("smartconfig_ble: GATT 0xFFFF / read 0xFF02 / write 0xFF01\n");
}

static void qcc74xsync_protocol_thread(void *p1, void *p2, void *p3)
{
	struct smartconfig_rx_packet packet;
	uint16_t mtu;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		if (k_msgq_get(&smartconfig_rx_queue, &packet, K_FOREVER) != 0) {
			continue;
		}

		mtu = current_conn != NULL ? bt_gatt_get_mtu(current_conn) - 3U : 20U;
		(void)qcc74xsync_transfer_receive(&transfer, packet.data,
						  packet.length, mtu);
	}
}

int qcc74xsync_ble_start(void)
{
	int err;

	printk("smartconfig_ble: protocol revision %s\n",
	       SMARTCONFIG_PROTOCOL_REVISION);
	err = qcc74xsync_transfer_init(&transfer, qcc74xsync_ble_send,
					qcc74xsync_ble_app_event, NULL);
	if (err != 0) {
		return err;
	}
	err = qcc74xsync_ble_app_init(&transfer);
	if (err != 0) {
		return err;
	}

	k_thread_create(&smartconfig_thread_data, smartconfig_thread_stack,
				K_THREAD_STACK_SIZEOF(smartconfig_thread_stack),
				qcc74xsync_protocol_thread, NULL, NULL, NULL, 5, 0,
				K_NO_WAIT);

	err = bt_enable(bluetooth_ready);
	if (err != 0) {
		return err;
	}

	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
