/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>

LOG_MODULE_REGISTER(eth_smoke, LOG_LEVEL_DBG);

int main(void)
{
	LOG_INF("Ethernet smoke test starting");

	struct net_if *iface = net_if_get_default();

	if (!iface) {
		LOG_ERR("No default network interface found");
		printk("ETHERNET SMOKE TEST FAILED: no interface\n");
		return -1;
	}

	LOG_INF("Default interface: %p", iface);

	int ret = net_if_up(iface);

	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("Failed to bring up Ethernet interface: %d", ret);
		printk("ETHERNET SMOKE TEST FAILED: net_if_up error %d\n", ret);
		return ret;
	}

	if (ret == -EALREADY) {
		LOG_INF("Ethernet interface up (ret=-EALREADY: already started by AUTO_START)");
	} else {
		LOG_INF("Ethernet interface up (ret=0: brought up now)");
	}

	bool carrier = net_if_is_carrier_ok(iface);
	bool admin   = net_if_is_admin_up(iface);
	LOG_INF("Interface state: admin=%s carrier=%s",
		admin   ? "UP" : "DOWN",
		carrier ? "ON" : "OFF");

	net_dhcpv4_start(iface);
	LOG_INF("DHCPv4 client started");
	printk("ETHERNET SMOKE TEST PASSED\n");

	return 0;
}
