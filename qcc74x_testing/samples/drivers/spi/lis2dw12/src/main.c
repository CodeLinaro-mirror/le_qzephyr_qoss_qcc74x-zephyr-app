/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_SPI_ASYNC
/* Signal and event for async SPI completion */
static struct k_poll_signal spi_done_sig;
static struct k_poll_event spi_done_evt = K_POLL_EVENT_INITIALIZER(
	K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &spi_done_sig);
#elif defined(CONFIG_SPI_QCC74X_DMA)
/* DMA sample worker context for background transfer demo */
#define DMA_WORK_THREAD_STACK_SIZE 1024
#define DMA_WORK_THREAD_PRIORITY   K_PRIO_PREEMPT(8)

struct spi_dma_work_ctx {
	const struct device *spi;
	const struct spi_config *cfg;
	const struct spi_buf_set *tx_set;
	const struct spi_buf_set *rx_set;
};

static struct spi_dma_work_ctx dma_work_ctx;
K_THREAD_STACK_DEFINE(dma_work_stack, DMA_WORK_THREAD_STACK_SIZE);
static struct k_thread dma_work_thread;
static struct k_sem dma_done_sem;
static volatile int dma_transfer_result;

static void dma_transfer_worker(void *p1, void *p2, void *p3)
{
	struct spi_dma_work_ctx *ctx = (struct spi_dma_work_ctx *)p1;
	int ret;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = spi_transceive(ctx->spi, ctx->cfg, ctx->tx_set, ctx->rx_set);
	dma_transfer_result = ret;
	k_sem_give(&dma_done_sem);
}
#endif

#define LIS2DW12_WHO_AM_I_REG   0x0F   // WHO_AM_I register
#define LIS2DW12_WHO_AM_I_VALUE 0x44   // Expected device ID
#define LIS2DW12_CTRL1_REG      0x20   // Control register 1
#define LIS2DW12_CTRL6_REG      0x25   // Control register 6
#define LIS2DW12_OUT_X_L_REG    0x28   // X-axis low byte
#define LIS2DW12_SENSITIVITY_2G 0.061f // mg/LSB for ±2g, high-performance

/* SPI configuration: x MHz, mode 3 (CPOL=1, CPHA=1), MSB first */
static const struct spi_config spi_cfg = {
	.frequency = 100000,
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
	.slave = 0,
};

/* Write a single register to LIS2DW12 */
static int lis2dw12_write_reg(const struct device *spi, const struct spi_config *cfg, uint8_t reg,
			      uint8_t value)
{
	uint8_t tx_buf[2] = {reg & 0x7F, value}; // Write: clear bit 7
	struct spi_buf tx = {.buf = tx_buf, .len = 2};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	int ret;

	ret = spi_write(spi, cfg, &tx_set);
	if (ret) {
		printk("SPI write failed: %d\n", ret);
		return ret;
	}
	return 0;
}

/* Read a single register from LIS2DW12 */
static int lis2dw12_read_reg(const struct device *spi, const struct spi_config *cfg, uint8_t reg,
			     uint8_t *data)
{
	uint8_t tx_buf[2] = {reg | 0x80, 0x00}; // Read: set bit 7
	uint8_t rx_buf[2];
	struct spi_buf tx = {.buf = tx_buf, .len = 2};
	struct spi_buf rx = {.buf = rx_buf, .len = 2};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
	int ret;

	ret = spi_transceive(spi, cfg, &tx_set, &rx_set);
	if (ret) {
		printk("SPI read failed: %d\n", ret);
		return ret;
	}
	*data = rx_buf[1];
	return 0;
}

/* Read 6 bytes (X, Y, Z) from LIS2DW12 starting at OUT_X_L */
static int lis2dw12_read_axes(const struct device *spi, const struct spi_config *cfg, int16_t *x,
			      int16_t *y, int16_t *z)
{
	uint8_t tx_buf[7] = {
		LIS2DW12_OUT_X_L_REG | 0x80, 0, 0, 0, 0, 0, 0}; // Read with auto-increment
	uint8_t rx_buf[7];
	struct spi_buf tx = {.buf = tx_buf, .len = 7};
	struct spi_buf rx = {.buf = rx_buf, .len = 7};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
	int ret;

#ifdef CONFIG_SPI_ASYNC
	/* Async mode: start transfer and do other work while waiting */
	k_poll_signal_reset(&spi_done_sig);
	spi_done_evt.state = K_POLL_STATE_NOT_READY;

	ret = spi_transceive_signal(spi, cfg, &tx_set, &rx_set, &spi_done_sig);
	if (ret) {
		printk("SPI async start failed: %d\n", ret);
		return ret;
	}

	/* Do useful work while SPI transfer happens in background */
	printk("[ASYNC] SPI transfer started, doing other work...\n");
	for (int i = 0; i < 3; i++) {
		printk("[ASYNC] Working... step %d\n", i + 1);
		k_busy_wait(100); /* Simulate some computation */
	}
	printk("[ASYNC] Work done, waiting for SPI completion...\n");

	/* Wait for completion with timeout */
	ret = k_poll(&spi_done_evt, 1, K_MSEC(1000));
	if (ret) {
		printk("SPI async timeout\n");
		return -ETIMEDOUT;
	}

	/* Check transfer result */
	if (spi_done_evt.signal->result != 0) {
		printk("SPI async transfer failed: %d\n", spi_done_evt.signal->result);
		return spi_done_evt.signal->result;
	}
	printk("[ASYNC] SPI transfer complete!\n");
#elif defined(CONFIG_SPI_QCC74X_DMA)
	/* DMA mode: run blocking API in worker thread to overlap app work */
	k_tid_t dma_tid;

	k_sem_reset(&dma_done_sem);
	dma_transfer_result = -EINPROGRESS;
	dma_work_ctx.spi = spi;
	dma_work_ctx.cfg = cfg;
	dma_work_ctx.tx_set = &tx_set;
	dma_work_ctx.rx_set = &rx_set;

	dma_tid = k_thread_create(&dma_work_thread, dma_work_stack,
				  K_THREAD_STACK_SIZEOF(dma_work_stack), dma_transfer_worker,
				  &dma_work_ctx, NULL, NULL, DMA_WORK_THREAD_PRIORITY, 0, K_NO_WAIT);
	if (dma_tid == NULL) {
		printk("[DMA] Failed to create worker thread\n");
		return -EIO;
	}

	printk("[DMA] SPI transfer started, doing other work...\n");
	for (int i = 0; i < 3; i++) {
		printk("[DMA] Working... step %d\n", i + 1);
		k_busy_wait(100); /* Simulate some computation */
	}
	printk("[DMA] Work done, waiting for SPI completion...\n");

	ret = k_sem_take(&dma_done_sem, K_MSEC(1000));
	if (ret) {
		printk("[DMA] SPI timeout\n");
		k_thread_abort(dma_tid);
		return -ETIMEDOUT;
	}

	if (dma_transfer_result != 0) {
		printk("[DMA] SPI transfer failed: %d\n", dma_transfer_result);
		return dma_transfer_result;
	}
	printk("[DMA] SPI transfer complete!\n");
#else
	/* Sync mode: blocking transfer */
	printk("[POLL] SPI transfer (blocking)...\n");
	ret = spi_transceive(spi, cfg, &tx_set, &rx_set);
	if (ret) {
		printk("[POLL] SPI read axes failed: %d\n", ret);
		return ret;
	}
	printk("[POLL] SPI transfer complete!\n");
#endif

	*x = (int16_t)((rx_buf[2] << 8) | rx_buf[1]);
	*y = (int16_t)((rx_buf[4] << 8) | rx_buf[3]);
	*z = (int16_t)((rx_buf[6] << 8) | rx_buf[5]);
	return 0;
}

/* Initialize LIS2DW12: 100 Hz, ±2g, high-performance mode */
static int lis2dw12_init(const struct device *spi, const struct spi_config *cfg)
{
	uint8_t device_id = 0;
	int ret;

	/* Verify device ID */
	ret = lis2dw12_read_reg(spi, cfg, LIS2DW12_WHO_AM_I_REG, &device_id);
	if (ret || device_id != LIS2DW12_WHO_AM_I_VALUE) {
		printk("Invalid device ID: 0x%02X, expected 0x%02X\n", device_id,
		       LIS2DW12_WHO_AM_I_VALUE);
		return -EIO;
	}

	printk("LIS2DW12 detected on SPI0 bus, WHO_AM_I: 0x%02X\n", device_id);

	/* Set CTRL1: ODR = 100 Hz (0b0101), high-performance mode */
	ret = lis2dw12_write_reg(spi, cfg, LIS2DW12_CTRL1_REG,
				 0x50); // 01010000: ODR=100Hz, MODE=high-performance
	if (ret) {
		printk("Failed to configure CTRL1\n");
		return ret;
	}

	/* Set CTRL6: ±2g full-scale */
	ret = lis2dw12_write_reg(spi, cfg, LIS2DW12_CTRL6_REG, 0x00); // 00xx0000: FS=±2g
	if (ret) {
		printk("Failed to configure CTRL6\n");
		return ret;
	}

	return 0;
}

int main(void)
{
	const struct device *spi;
	int16_t x_raw = 0, y_raw = 0, z_raw = 0;
	float x_g, y_g, z_g;
	int ret;

	k_msleep(1000);

	/* Get SPI device */
	spi = DEVICE_DT_GET(DT_NODELABEL(spi0));
	if (!spi || !device_is_ready(spi)) {
		printk("SPI device spi0 not ready\n");
		return 0;
	}

#ifdef CONFIG_SPI_ASYNC
	/* Initialize async signal */
	k_poll_signal_init(&spi_done_sig);
	printk("Using ASYNC SPI mode (interrupt-driven)\n");
#elif defined(CONFIG_SPI_QCC74X_DMA)
	k_sem_init(&dma_done_sem, 0, 1);
	printk("Using SYNC SPI mode (DMA backend)\n");
#elif defined(CONFIG_SPI_QCC74X_INTERRUPT)
	printk("Using SYNC SPI mode (interrupt-driven)\n");
#else
	printk("Using SYNC SPI mode (polling)\n");
#endif

	/* Initialize LIS2DW12 */
	ret = lis2dw12_init(spi, &spi_cfg);
	if (ret) {
		printk("LIS2DW12 initialization failed\n");
		return 0;
	}

	printk("LIS2DW12 initialized successfully\n");

	/* Main loop: Read and print accelerometer data */
	while (1) {
		ret = lis2dw12_read_axes(spi, &spi_cfg, &x_raw, &y_raw, &z_raw);
		if (ret) {
			printk("Failed to read accelerometer data\n");
		} else {
			/* Convert raw data to g (sensitivity: 0.061 mg/LSB for ±2g) */
			x_g = (x_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;
			y_g = (y_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;
			z_g = (z_raw * LIS2DW12_SENSITIVITY_2G) / 1000.0f;

			printk("Accel (g): X=%.3f, Y=%.3f, Z=%.3f\n", (double)x_g, (double)y_g,
			       (double)z_g);
		}

		k_msleep(1000);
	}
}
