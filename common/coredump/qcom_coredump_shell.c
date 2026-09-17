/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/shell/shell.h>
#include <zephyr/soc/qualcomm/qcc74x_coredump_memory.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "qcc74x_coredump_flash_format.h"

static const char *qcom_coredump_backend(void) {
  if (IS_ENABLED(CONFIG_DEBUG_COREDUMP_BACKEND_OTHER)) {
    return "qcc74x-safe-serial";
  }

  return IS_ENABLED(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION) ? "flash"
                                                                   : "logging";
}

static const char *qcom_coredump_psram_coverage(void) {
  if (!IS_ENABLED(CONFIG_QCC74X_COREDUMP_FULL_MEMORY)) {
    return "minimal";
  }

  return IS_ENABLED(CONFIG_QCC74X_COREDUMP_SERIAL_FULL_PSRAM) ? "full_psram"
                                                              : "tracked_psram";
}

static int cmd_qcom_coredump_status(const struct shell *shell, size_t argc,
                                    char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  shell_print(shell, "backend: %s", qcom_coredump_backend());
  shell_print(shell, "memory profile: %s", qcom_coredump_psram_coverage());
  shell_print(shell, "PSRAM ledger: active=%u overflow=%u",
              (unsigned int)qcc74x_coredump_psram_ledger_active_count(),
              qcc74x_coredump_psram_ledger_overflowed() ? 1U : 0U);
  return 0;
}

static int cmd_qcom_coredump_coverage(const struct shell *shell, size_t argc,
                                      char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  if (!IS_ENABLED(CONFIG_QCC74X_COREDUMP_FULL_MEMORY)) {
    shell_print(shell, "coverage: Zephyr minimal coredump memory");
    return 0;
  }

  shell_print(shell, "coverage: OCRAM, Wi-Fi WRAM, HBN RAM, and %s",
              IS_ENABLED(CONFIG_QCC74X_COREDUMP_SERIAL_FULL_PSRAM)
                  ? "the complete DTS PSRAM window"
                  : "static PSRAM plus tracked active allocations");
  return 0;
}

#if defined(CONFIG_QCC74X_COREDUMP_FULL_MEMORY) &&                             \
    defined(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION)
static int cmd_qcom_coredump_budget(const struct shell *shell, size_t argc,
                                    char **argv) {
  struct qcc74x_coredump_flash_budget budget;
  int result;

  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  result = qcc74x_coredump_flash_budget_check(true, &budget);
  shell_print(shell, "regions=%zu data=%zu headers=%zu fixed=%zu",
              budget.region_count, budget.region_data_bytes,
              budget.region_header_bytes, budget.fixed_payload_bytes);
  shell_print(shell, "required=%zu slot=%zu remaining=%zu",
              budget.required_flash_bytes, budget.slot_bytes,
              budget.remaining_bytes);
  if (result == 0) {
    shell_print(shell, "budget: PASS");
    return 0;
  }

  shell_error(shell, "budget: REJECT err=%d ledger_overflow=%u", result,
              budget.psram_ledger_overflow ? 1U : 0U);
  return result;
}
#endif

#if defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH)
#define QCC74X_COREDUMP_FLASH_STORAGE_NODE                                      \
  DT_PARENT(DT_PARENT(DT_NODELABEL(coredump_partition)))
#define QCC74X_COREDUMP_FLASH_DEVICE_NODE                                      \
  DT_PARENT(QCC74X_COREDUMP_FLASH_STORAGE_NODE)
#define QCC74X_COREDUMP_FLASH_SLOT_OFFSET                                      \
  DT_REG_ADDR(DT_NODELABEL(coredump_partition))
#define QCC74X_COREDUMP_FLASH_SLOT_SIZE                                        \
  DT_REG_SIZE(DT_NODELABEL(coredump_partition))
#define QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET                                  \
  DT_PROP(QCC74X_COREDUMP_FLASH_STORAGE_NODE, write_block_size)
#define QCC74X_COREDUMP_FLASH_EXPORT_LINE_BYTES 32U

static char qcc74x_coredump_flash_export_line[
    4U + (2U * QCC74X_COREDUMP_FLASH_EXPORT_LINE_BYTES) + 1U];

static char qcc74x_coredump_hex(uint8_t value) {
  return value < 10U ? (char)('0' + value) : (char)('a' + (value - 10U));
}

static int qcc74x_coredump_flash_read_header(
    struct qcc74x_coredump_flash_header *header) {
  const struct device *flash = DEVICE_DT_GET(QCC74X_COREDUMP_FLASH_DEVICE_NODE);
  int result;

  if (!device_is_ready(flash)) {
    return -ENODEV;
  }

  result = flash_read(flash, QCC74X_COREDUMP_FLASH_SLOT_OFFSET, header,
                      sizeof(*header));
  if (result != 0) {
    return result;
  }

  if (header->magic != QCC74X_COREDUMP_FLASH_MAGIC ||
      header->version != QCC74X_COREDUMP_FLASH_VERSION ||
      header->header_size != sizeof(*header) ||
      header->state != QCC74X_COREDUMP_FLASH_STATE_VALID) {
    return -ENODATA;
  }

  if (header->payload_length == 0U ||
      header->payload_length > (QCC74X_COREDUMP_FLASH_SLOT_SIZE -
                                QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET)) {
    return -EFBIG;
  }

  return 0;
}

static int cmd_qcom_coredump_flash_status(const struct shell *shell,
                                          size_t argc, char **argv) {
  struct qcc74x_coredump_flash_header header;
  int result;

  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  result = qcc74x_coredump_flash_read_header(&header);
  if (result != 0) {
    shell_error(shell, "Flash coredump unavailable: err=%d", result);
    return result;
  }

  shell_print(shell, "Flash coredump: valid payload=%u bytes crc=0x%08x",
              header.payload_length, header.payload_crc);
  return 0;
}

static int cmd_qcom_coredump_flash_export(const struct shell *shell,
                                          size_t argc, char **argv) {
  const struct device *flash = DEVICE_DT_GET(QCC74X_COREDUMP_FLASH_DEVICE_NODE);
  struct qcc74x_coredump_flash_header header;
  uint8_t buffer[QCC74X_COREDUMP_FLASH_EXPORT_LINE_BYTES];
  uint32_t crc = 0U;
  size_t offset = 0U;
  int result;

  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  result = qcc74x_coredump_flash_read_header(&header);
  if (result != 0) {
    shell_error(shell, "Flash coredump export rejected: err=%d", result);
    return result;
  }

  shell_print(shell, "#CD:BEGIN#");
  while (offset < header.payload_length) {
    size_t bytes = MIN(sizeof(buffer), header.payload_length - offset);
    size_t line_index = 0U;

    result = flash_read(flash, QCC74X_COREDUMP_FLASH_SLOT_OFFSET +
                                   QCC74X_COREDUMP_FLASH_PAYLOAD_OFFSET + offset,
                        buffer, bytes);
    if (result != 0) {
      shell_error(shell, "Flash coredump read failed at %u: err=%d",
                  (unsigned int)offset, result);
      return result;
    }

    crc = crc32_ieee_update(crc, buffer, bytes);
    qcc74x_coredump_flash_export_line[line_index++] = '#';
    qcc74x_coredump_flash_export_line[line_index++] = 'C';
    qcc74x_coredump_flash_export_line[line_index++] = 'D';
    qcc74x_coredump_flash_export_line[line_index++] = ':';
    for (size_t index = 0U; index < bytes; index++) {
      qcc74x_coredump_flash_export_line[line_index++] =
          qcc74x_coredump_hex(buffer[index] >> 4U);
      qcc74x_coredump_flash_export_line[line_index++] =
          qcc74x_coredump_hex(buffer[index] & 0x0fU);
    }
    qcc74x_coredump_flash_export_line[line_index] = '\0';
    shell_print(shell, "%s", qcc74x_coredump_flash_export_line);
    offset += bytes;
  }

  if (crc != header.payload_crc) {
    shell_error(shell,
                "Flash coredump export rejected: CRC mismatch stored=0x%08x "
                "calculated=0x%08x",
                header.payload_crc, crc);
    return -EBADMSG;
  }

  shell_print(shell, "#CD:END#");
  shell_print(shell, "Flash coredump export complete: %u bytes",
              header.payload_length);
  return 0;
}
#endif

#if defined(CONFIG_QCC74X_COREDUMP_TEST_COMMANDS)
static int cmd_qcom_coredump_panic(const struct shell *shell, size_t argc,
                                   char **argv) {
  ARG_UNUSED(shell);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  k_panic();
  return 0;
}

__no_optimization static int cmd_qcom_coredump_illegal(
    const struct shell *shell, size_t argc, char **argv) {
  ARG_UNUSED(shell);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  __asm__ volatile(".word 0xffffffff");
  return 0;
}

__no_optimization static int cmd_qcom_coredump_ebreak(
    const struct shell *shell, size_t argc, char **argv) {
  ARG_UNUSED(shell);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  __asm__ volatile("ebreak");
  return 0;
}
#endif

#if defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH)
SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_qcom_coredump_flash,
    SHELL_CMD(status, NULL, "Show the stored Flash coredump status",
              cmd_qcom_coredump_flash_status),
    SHELL_CMD(export, NULL, "Export the stored Flash coredump as #CD records",
              cmd_qcom_coredump_flash_export),
    SHELL_SUBCMD_SET_END);
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_qcom_coredump,
    SHELL_CMD(status, NULL, "Show coredump runtime configuration",
              cmd_qcom_coredump_status),
    SHELL_CMD(coverage, NULL, "Show configured RAM coverage",
              cmd_qcom_coredump_coverage),
#if defined(CONFIG_QCC74X_COREDUMP_FULL_MEMORY) &&                             \
    defined(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION)
    SHELL_CMD(budget, NULL, "Check full-memory Flash coredump budget",
              cmd_qcom_coredump_budget),
#endif
#if defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH)
    SHELL_CMD(flash, NULL, "Inspect or export the stored Flash coredump",
              &sub_qcom_coredump_flash),
#endif
#if defined(CONFIG_QCC74X_COREDUMP_TEST_COMMANDS)
    SHELL_CMD(panic, NULL, "Trigger a kernel panic coredump",
              cmd_qcom_coredump_panic),
    SHELL_CMD(ebreak, NULL, "Trigger a RISC-V ebreak coredump",
              cmd_qcom_coredump_ebreak),
    SHELL_CMD(illegal, NULL,
              "Diagnostic RISC-V illegal instruction (may bypass coredump)",
              cmd_qcom_coredump_illegal),
#endif
    SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((qcom), coredump, &sub_qcom_coredump,
                 "QCC74x coredump runtime commands", NULL, 1, 0);
