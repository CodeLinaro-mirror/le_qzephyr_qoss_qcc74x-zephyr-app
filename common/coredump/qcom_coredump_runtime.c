/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/debug/coredump.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/soc/qualcomm/qcc74x_coredump_memory.h>

#include <qc7xx_sys.h>

#if defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_SERIAL)
int qcom_coredump_backend_other_flush(void);
#endif

#if defined(CONFIG_QCC74X_COREDUMP_WATCHDOG_COORDINATION)
#if !DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#error "QCOM coredump watchdog coordination requires an enabled watchdog0"
#endif

static const struct device *const coredump_watchdog =
    DEVICE_DT_GET(DT_ALIAS(watchdog0));
#endif

#if (defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING) ||                              \
     defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_SERIAL)) && DT_HAS_CHOSEN(zephyr_console)
static const struct device *const coredump_console =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#define QCC74X_COREDUMP_CONSOLE_AVAILABLE 1
#endif

#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)
#if defined(CONFIG_QCC74X_COREDUMP_SERIAL_FULL_PSRAM)
#define QCOM_SERIAL_COREDUMP_PROFILE "full_psram"
#define QCOM_SERIAL_COREDUMP_BAUD 2000000U
#elif defined(CONFIG_QCC74X_COREDUMP_SERIAL_TRACKED_PSRAM)
#define QCOM_SERIAL_COREDUMP_PROFILE "tracked_psram"
#define QCOM_SERIAL_COREDUMP_BAUD 115200U
#else
#define QCOM_SERIAL_COREDUMP_PROFILE "minimal"
#define QCOM_SERIAL_COREDUMP_BAUD 0U
#endif
#endif

void __real_coredump(unsigned int reason, const struct arch_esf *esf,
                     struct k_thread *thread);

#if defined(CONFIG_QCC74X_COREDUMP_WATCHDOG_COORDINATION)
static void qcom_coredump_watchdog_disable(void) {
  if (device_is_ready(coredump_watchdog)) {
    (void)wdt_disable(coredump_watchdog);
  }
}
#endif

#if defined(QCC74X_COREDUMP_CONSOLE_AVAILABLE)
static void qcom_coredump_tx_drain(void) {
#if defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_SERIAL)
  (void)qcom_coredump_backend_other_flush();
#else
  int tx_complete = -ENODEV;

  if (device_is_ready(coredump_console)) {
    tx_complete = uart_irq_tx_complete(coredump_console);
  }

  if (tx_complete != 1) {
    printk("QCC74X_SERIAL_COREDUMP_TX_DRAIN_TIMEOUT status=%d\n", tx_complete);
    k_busy_wait(100U * USEC_PER_MSEC);
  }
#endif
}
#endif

#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)
static void qcom_coredump_log_result(unsigned int reason, bool dumped) {
  printk("QCC74X_SERIAL_COREDUMP_%s profile=%s baud=%u reason=%u "
         "ledger_overflow=%u active_allocations=%u\n",
         dumped ? "COMPLETE" : "REJECT", QCOM_SERIAL_COREDUMP_PROFILE,
         QCOM_SERIAL_COREDUMP_BAUD, reason,
         qcc74x_coredump_psram_ledger_overflowed() ? 1U : 0U,
         (unsigned int)qcc74x_coredump_psram_ledger_active_count());
}
#endif

#if defined(CONFIG_QCC74X_COREDUMP_AUTO_RESET)
static FUNC_NORETURN void qcom_coredump_finish(unsigned int reason) {
#if defined(QCC74X_COREDUMP_CONSOLE_AVAILABLE)
  qcom_coredump_tx_drain();
#endif

  /*
   * Use the QCC74x POR reset helper also used by "platform reboot --por".
   * QCC748M validation shows that this path recovers reliably across repeated
   * resets, unlike the narrower GLB software-reset path. Do not use Zephyr's
   * sys_reboot(): its QCC74x implementation follows a separate reset path.
   */
  (void)qc7xx_sys_reset_por();
  k_fatal_halt(reason);
}
#else
static void qcom_coredump_finish(unsigned int reason) {
  ARG_UNUSED(reason);
}
#endif

void __wrap_coredump(unsigned int reason, const struct arch_esf *esf,
                     struct k_thread *thread) {
#if defined(CONFIG_QCC74X_COREDUMP_FULL_MEMORY) &&                             \
    (defined(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION) ||                   \
     defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH))
  struct qcc74x_coredump_flash_budget budget;
  int budget_result = qcc74x_coredump_flash_budget_check(esf != NULL, &budget);

  if (budget_result != 0) {
#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)
    printk("QCC74X_COREDUMP_REJECT reason=%u err=%d regions=%zu "
           "planned=%zu required=%zu slot=%zu ledger_overflow=%u\n",
           reason, budget_result, budget.region_count,
           budget.planned_payload_bytes, budget.required_flash_bytes,
           budget.slot_bytes, budget.psram_ledger_overflow ? 1U : 0U);
#endif
    qcom_coredump_finish(reason);
    return;
  }
#endif

#if defined(CONFIG_QCC74X_COREDUMP_WATCHDOG_COORDINATION)
  qcom_coredump_watchdog_disable();
#endif

#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)
  if (IS_ENABLED(CONFIG_QCC74X_COREDUMP_SERIAL_TRACKED_PSRAM) &&
      qcc74x_coredump_psram_ledger_overflowed()) {
    qcom_coredump_log_result(reason, false);
    qcom_coredump_finish(reason);
    return;
  }

  printk("QCC74X_SERIAL_COREDUMP_BEGIN profile=%s baud=%u reason=%u\n",
         QCOM_SERIAL_COREDUMP_PROFILE, QCOM_SERIAL_COREDUMP_BAUD, reason);
#endif

  __real_coredump(reason, esf, thread);

#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)
  qcom_coredump_log_result(reason, true);
#endif

  qcom_coredump_finish(reason);
}
