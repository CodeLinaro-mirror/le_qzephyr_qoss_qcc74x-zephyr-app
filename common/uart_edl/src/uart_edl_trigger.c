/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Lightweight UART EDL trigger.
 *
 * Watches the console UART for the string "platform reboot --edl\r\n"
 * (the same command the shell accepts).  When the full string is received
 * the chip immediately enters Emergency Download mode, exactly as the
 * shell "platform reboot --edl" command does.
 *
 * Using the same trigger string as the shell means the host-side script
 * (qconn_pre_flash.py) never needs to know whether the running firmware
 * has CONFIG_SHELL or CONFIG_QCC74X_UART_EDL_TRIGGER – it always sends
 * the same bytes.
 *
 * How it works
 * ------------
 * Without CONFIG_SHELL the console UART RX path is idle: nothing reads
 * incoming bytes.  This module installs an interrupt-driven RX callback
 * (requires CONFIG_UART_INTERRUPT_DRIVEN=y) that runs a byte-by-byte
 * string matcher.  The TX path (printk / LOG) is unaffected.
 *
 * Host side (unchanged from shell mode)
 * --------------------------------------
 *   ser.write(b"platform reboot --edl\r\n")
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <qcc74x_hbn.h>   /* HBN_Set_User_Boot_Config() */
#include <qcc74x_glb.h>   /* GLB_SW_POR_Reset() */

/* Trigger string – identical to the shell command so the host script
 * never needs to distinguish between shell and no-shell firmware.
 */
static const uint8_t edl_magic[] = "platform reboot --edl\r\n";
/* Length excludes the NUL terminator added by the string literal. */
#define EDL_MAGIC_LEN (sizeof(edl_magic) - 1)

/* Current match position (0..EDL_MAGIC_LEN-1).
 * Only accessed from the UART ISR, so no locking needed.
 *
 * Force into .bss (not .sbss) so it is never accessed via the GP register.
 * On RISC-V the GP window is only ±2 KB; even one byte in .sbss can push
 * other objects out of range and cause
 * "relocation truncated to fit: R_RISCV_GPREL_I" link errors.
 * zephyr_library_compile_options(-msmall-data-limit=0) in CMakeLists.txt
 * is the primary guard, but the attribute is a belt-and-suspenders fallback
 * in case the inherited zephyr_interface options override it.
 */
static uint8_t match_pos __attribute__((section(".bss")));

/**
 * Enter EDL (Emergency Download) mode.
 *
 * Equivalent to qc7xx_sys_reset_system_from_interface() but without
 * pulling in the entire qcc74x_sys.c (which adds ~1KB of static data
 * that can overflow the RISC-V GP-relative section on minimal builds).
 */
static void enter_edl_mode(void)
{
	unsigned int key = irq_lock();

	/* Tell bootloader to enter EDL on next boot */
	HBN_Set_User_Boot_Config(1);
	/* Power-on-reset the entire chip */
	GLB_SW_POR_Reset();

	/* Should not reach here */
	while (1) {
	}

	irq_unlock(key); /* unreachable, keeps compiler happy */
}

static void uart_edl_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}

		if (c == edl_magic[match_pos]) {
			match_pos++;
			if (match_pos == EDL_MAGIC_LEN) {
				/* Full sequence matched – enter EDL mode.
				 * This call does not return.
				 */
			printk("[uart_edl_trigger] entering EDL mode...\n");
			enter_edl_mode();
			match_pos = 0; /* unreachable, defensive */
			}
		} else {
			/* Mismatch: restart from the beginning, but check
			 * whether the current byte starts a new match.
			 */
			match_pos = 0;
			if (c == edl_magic[0]) {
				match_pos = 1;
			}
		}
	}
}

static int uart_edl_trigger_init(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	uart_irq_callback_user_data_set(uart, uart_edl_cb, NULL);
	uart_irq_rx_enable(uart);

	return 0;
}

/* Initialise after the UART driver (POST_KERNEL) but before the
 * application so the listener is active from the very first byte.
 */
SYS_INIT(uart_edl_trigger_init, POST_KERNEL, 99);