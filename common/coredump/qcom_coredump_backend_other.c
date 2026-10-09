/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <compiler/compiler_ld.h>
#include <hardware/uart_reg.h>

#if !DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
#error "QCC74x coredump safe serial backend requires zephyr,console"
#endif

#define QCC74X_COREDUMP_UART_BASE DT_REG_ADDR(DT_CHOSEN(zephyr_console))
#define QCC74X_COREDUMP_LINE_BYTES 32U
#define QCC74X_COREDUMP_LINE_LENGTH (4U + (2U * QCC74X_COREDUMP_LINE_BYTES) + 2U)

static int coredump_error;
static char coredump_line[QCC74X_COREDUMP_LINE_LENGTH];

static ATTR_TCM_SECTION int qcom_coredump_uart_putc(uint8_t value) {
  for (uint32_t poll = 0U; poll < CONFIG_QCC74X_COREDUMP_OTHER_UART_POLL_LIMIT;
       poll++) {
    if ((sys_read32(QCC74X_COREDUMP_UART_BASE + UART_FIFO_CONFIG_1_OFFSET) &
         UART_TX_FIFO_CNT_MASK) != 0U) {
      sys_write8(value, QCC74X_COREDUMP_UART_BASE + UART_FIFO_WDATA_OFFSET);
      return 0;
    }
  }

  return -ETIMEDOUT;
}

static ATTR_TCM_SECTION int qcom_coredump_uart_write(const uint8_t *data,
                                                     size_t length) {
  if (data == NULL) {
    return -EINVAL;
  }

  for (size_t index = 0U; index < length; index++) {
    int result = qcom_coredump_uart_putc(data[index]);

    if (result != 0) {
      return result;
    }
  }

  return 0;
}

int qcom_coredump_backend_other_flush(void) {
  for (uint32_t poll = 0U; poll < CONFIG_QCC74X_COREDUMP_OTHER_UART_POLL_LIMIT;
       poll++) {
    uint32_t fifo_count =
        sys_read32(QCC74X_COREDUMP_UART_BASE + UART_FIFO_CONFIG_1_OFFSET) &
        UART_TX_FIFO_CNT_MASK;

    if (fifo_count == ((UART_TX_FIFO_CNT_MASK >> 1U) + 1U)) {
      return 0;
    }
  }

  return -ETIMEDOUT;
}

static char qcom_coredump_hex(uint8_t value) {
  return value < 10U ? (char)('0' + value) : (char)('a' + (value - 10U));
}

static void qcom_coredump_other_start(void) {
  static const uint8_t marker[] = "QCC74X_SAFE_SERIAL_COREDUMP_BEGIN\r\n";
  static const uint8_t begin[] = "#CD:BEGIN#\r\n";

  coredump_error = qcom_coredump_uart_write(marker, sizeof(marker) - 1U);
  if (coredump_error == 0) {
    coredump_error = qcom_coredump_uart_write(begin, sizeof(begin) - 1U);
  }
}

static void qcom_coredump_other_buffer_output(uint8_t *buffer, size_t length) {
  if (coredump_error != 0) {
    return;
  }

  if (buffer == NULL || length == 0U) {
    coredump_error = -EINVAL;
    return;
  }

  while (length > 0U) {
    size_t line_bytes = MIN(length, QCC74X_COREDUMP_LINE_BYTES);
    size_t line_index = 0U;

    coredump_line[line_index++] = '#';
    coredump_line[line_index++] = 'C';
    coredump_line[line_index++] = 'D';
    coredump_line[line_index++] = ':';

    for (size_t index = 0U; index < line_bytes; index++) {
      coredump_line[line_index++] = qcom_coredump_hex(buffer[index] >> 4U);
      coredump_line[line_index++] = qcom_coredump_hex(buffer[index] & 0x0fU);
    }

    coredump_line[line_index++] = '\r';
    coredump_line[line_index++] = '\n';
    coredump_error =
        qcom_coredump_uart_write((const uint8_t *)coredump_line, line_index);
    if (coredump_error != 0) {
      return;
    }

    buffer += line_bytes;
    length -= line_bytes;
  }
}

static void qcom_coredump_other_end(void) {
  static const uint8_t complete[] = "#CD:END#\r\n";
  static const uint8_t marker[] = "QCC74X_SAFE_SERIAL_COREDUMP_COMPLETE\r\n";
  static const uint8_t failed[] = "#CD:ERROR#\r\n";

  if (coredump_error == 0) {
    coredump_error = qcom_coredump_uart_write(complete, sizeof(complete) - 1U);
    if (coredump_error == 0) {
      coredump_error = qcom_coredump_uart_write(marker, sizeof(marker) - 1U);
    }
    if (coredump_error == 0) {
      coredump_error = qcom_coredump_backend_other_flush();
    }
  } else {
    (void)qcom_coredump_uart_write(failed, sizeof(failed) - 1U);
  }
}

static int qcom_coredump_other_query(enum coredump_query_id query_id,
                                     void *arg) {
  ARG_UNUSED(arg);

  if (query_id == COREDUMP_QUERY_GET_ERROR) {
    return coredump_error;
  }

  return -ENOTSUP;
}

static int qcom_coredump_other_cmd(enum coredump_cmd_id cmd_id, void *arg) {
  ARG_UNUSED(arg);

  if (cmd_id == COREDUMP_CMD_CLEAR_ERROR) {
    coredump_error = 0;
    return 0;
  }

  return -ENOTSUP;
}

struct coredump_backend_api coredump_backend_other = {
    .start = qcom_coredump_other_start,
    .end = qcom_coredump_other_end,
    .buffer_output = qcom_coredump_other_buffer_output,
    .query = qcom_coredump_other_query,
    .cmd = qcom_coredump_other_cmd,
};
