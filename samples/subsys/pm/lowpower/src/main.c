/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>

#include <qc7xx_hbn_system.h>
#include <qcc74x_pm.h>

LOG_MODULE_REGISTER(qcc74x_pm_demo, LOG_LEVEL_INF);

#define HBN_WAKE_GPIO 2U
#define QCC74X_PM_PDS_SUBSTATE_ID 15U
#define CONSOLE_SETTLE_US 10000U

/*
 * LPFW references this Wi-Fi export unconditionally.  This radio-free demo
 * has no RX buffer, so provide the link symbol and leave the field null.
 */
uint32_t *export_get_rx_buffer1_addr(void)
{
	return NULL;
}

int main(void)
{
	struct pm_policy_latency_request latency_req = { 0 };
	struct qc7xx_hbn_acomp_config hbn_acomp = {
		.input_pin = HBN_WAKE_GPIO,
		.trigger = QC7XX_HBN_ACOMP_TRIGGER_RISING,
	};
	int rc;

	LOG_INF("QCC74x PM low-power demo booting");

	for (;;) {
		rc = qcc74x_pm_pds15_auto_enable(true);
		if (rc == 0) {
			break;
		}

		if (rc != -EAGAIN && rc != -EINPROGRESS) {
			LOG_ERR("PDS automatic policy enable failed: %d", rc);
			return rc;
		}

		k_sleep(K_MSEC(100));
	}

	pm_policy_latency_request_add(&latency_req, 1000U);
	LOG_INF("PDS blocked by 1 ms maximum wake latency");
	k_sleep(K_SECONDS(10));

	/*
	 * The console's asynchronous TX path is still busy just after LOG_INF().
	 * If PDS is eligible at that instant, the policy falls back to shallow idle
	 * and, with no earlier interrupt, does not retry until the 10 s deadline.
	 * Keep PDS locked while the console drains, then release it immediately
	 * before the sole PDS sleep interval.
	 */
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
				 QCC74X_PM_PDS_SUBSTATE_ID);
	pm_policy_latency_request_update(&latency_req, 10000U);
	LOG_INF("PDS eligible with 10 ms maximum wake latency");
	LOG_INF("Enter PDS sleep");
	/* Let the UART complete its final hardware shift before PDS can start. */
	k_busy_wait(CONSOLE_SETTLE_US);

	pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM,
				 QCC74X_PM_PDS_SUBSTATE_ID);
	k_sleep(K_SECONDS(10));

	/* Keep PDS locked while the resume and HBN-entry logs settle on UART. */
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
				 QCC74X_PM_PDS_SUBSTATE_ID);
	LOG_INF("Exit from PDS sleep");

	/*
	 * Do not enable CONFIG_POWEROFF here: its QCC74x system-off backend
	 * selects SOC_EARLY_INIT_HOOK, which is not compatible with this PDS
	 * resume path. PDS already links these HAL HBN primitives.
	 */
	qc7xx_hbn_boot2_init(0U, 0U, 0U, 0U);
	qc7xx_hbn_entry_begin();
	qc7xx_hbn_acomp_disable_all();
	rc = qc7xx_hbn_acomp_select_id(hbn_acomp.input_pin, &hbn_acomp.id);
	if (rc == 0) {
		rc = qc7xx_hbn_acomp_apply(&hbn_acomp);
	}
	if (rc != 0) {
		LOG_ERR("HBN GPIO%u ACOMP configuration failed: %d", HBN_WAKE_GPIO, rc);
		return rc;
	}

	LOG_INF("Enter HBN soft-off; GPIO%u rising edge is the only wake source",
		HBN_WAKE_GPIO);
	k_busy_wait(CONSOLE_SETTLE_US);
	qc7xx_hbn_system_enter(PM_HBN_LEVEL_0, 0U);
}
