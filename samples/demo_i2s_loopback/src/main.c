/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

#if DT_NODE_EXISTS(DT_NODELABEL(i2s_rxtx))
#define I2S_RX_NODE DT_ALIAS(i2s_rxtx)
#define I2S_TX_NODE I2S_RX_NODE
#else
#define I2S_RX_NODE DT_ALIAS(i2s_rx)
#define I2S_TX_NODE DT_ALIAS(i2s_tx)
#endif

#define SAMPLE_FREQUENCY   44100
#define SAMPLE_BIT_WIDTH   16
#define BYTES_PER_SAMPLE   sizeof(int16_t)
#define NUMBER_OF_CHANNELS 2
#define SAMPLES_PER_BLOCK  ((SAMPLE_FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS     2
#define TIMEOUT            1000

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + 2)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

#define SEND_BLOCK_COUNT (10U)

static K_SEM_DEFINE(toggle_transfer, 1, 1);

static int16_t tx_mem_sample = 0;
static int16_t rx_mem_sample = 0;

/**
 * @brief Generate block data with sample values.
 * 
 * Sample values increase by 1 by every sample. Sample values can wrap around.
 */
static void generate_block_data(void *mem_block, uint32_t number_of_samples)
{
	for (int i = 0; i < number_of_samples; ++i) {
		((int16_t *)mem_block)[i] = ++tx_mem_sample;
	}
}

static bool configure_streams(const struct device *i2s_dev_rx, const struct device *i2s_dev_tx,
			      const struct i2s_config *config)
{
	int ret;

	if (i2s_dev_rx == i2s_dev_tx) {
		ret = i2s_configure(i2s_dev_rx, I2S_DIR_BOTH, config);
		if (ret == 0) {
			return true;
		}
		/* -ENOSYS means that the RX and TX streams need to be
		 * configured separately. */
		if (ret != -ENOSYS) {
			printk("Failed to configure streams: %d\n", ret);
			return false;
		}
	}

	ret = i2s_configure(i2s_dev_rx, I2S_DIR_RX, config);
	if (ret < 0) {
		printk("Failed to configure RX stream: %d\n", ret);
		return false;
	}

	ret = i2s_configure(i2s_dev_tx, I2S_DIR_TX, config);
	if (ret < 0) {
		printk("Failed to configure TX stream: %d\n", ret);
		return false;
	}

	return true;
}

static bool prepare_transfer(const struct device *i2s_dev_rx, const struct device *i2s_dev_tx)
{
	int ret;

	for (int i = 0; i < INITIAL_BLOCKS; ++i) {
		void *mem_block;

		ret = k_mem_slab_alloc(&mem_slab, &mem_block, K_NO_WAIT);
		if (ret < 0) {
			printk("Failed to allocate TX block %d: %d\n", i, ret);
			return false;
		}

		generate_block_data(mem_block, SAMPLES_PER_BLOCK);

		ret = i2s_write(i2s_dev_tx, mem_block, BLOCK_SIZE);
		if (ret < 0) {
			printk("Failed to write block %d: %d\n", i, ret);
			return false;
		}
	}

	return true;
}

static bool trigger_command(const struct device *i2s_dev_rx, const struct device *i2s_dev_tx,
			    enum i2s_trigger_cmd cmd)
{
	int ret;

	if (i2s_dev_rx == i2s_dev_tx) {
		ret = i2s_trigger(i2s_dev_rx, I2S_DIR_BOTH, cmd);
		if (ret == 0) {
			return true;
		}
		/* -ENOSYS means that commands for the RX and TX streams need
		 * to be triggered separately. */
		if (ret != -ENOSYS) {
			printk("Failed to trigger command %d: %d\n", cmd, ret);
			return false;
		}
	}

	ret = i2s_trigger(i2s_dev_rx, I2S_DIR_RX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on RX: %d\n", cmd, ret);
		return false;
	}

	ret = i2s_trigger(i2s_dev_tx, I2S_DIR_TX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on TX: %d\n", cmd, ret);
		return false;
	}

	return true;
}

int main(void)
{
	const struct device *const i2s_dev_rx = DEVICE_DT_GET(I2S_RX_NODE);
	const struct device *const i2s_dev_tx = DEVICE_DT_GET(I2S_TX_NODE);
	struct i2s_config config;

	if (!device_is_ready(i2s_dev_rx)) {
		printk("%s is not ready\n", i2s_dev_rx->name);
		return 0;
	}

	if (i2s_dev_rx != i2s_dev_tx && !device_is_ready(i2s_dev_tx)) {
		printk("%s is not ready\n", i2s_dev_tx->name);
		return 0;
	}

	config.word_size = SAMPLE_BIT_WIDTH;
	config.channels = NUMBER_OF_CHANNELS;
	config.format = I2S_FMT_DATA_FORMAT_I2S;
	config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
	config.frame_clk_freq = SAMPLE_FREQUENCY;
	config.mem_slab = &mem_slab;
	config.block_size = BLOCK_SIZE;
	config.timeout = TIMEOUT;
	if (!configure_streams(i2s_dev_rx, i2s_dev_tx, &config)) {
		return 0;
	}

	for (;;) {
		k_sem_take(&toggle_transfer, K_FOREVER);

		if (!prepare_transfer(i2s_dev_rx, i2s_dev_tx)) {
			return 0;
		}

		if (!trigger_command(i2s_dev_rx, i2s_dev_tx, I2S_TRIGGER_START)) {
			return 0;
		}

		printk("Streams started\n");

		int blocks_sent = 0;
		void *mem_block;
		int sample_mismatch_count = 0;
		int checked_sample_count = 0;
		bool rx_mem_sample_is_set = false;

		while (k_sem_take(&toggle_transfer, K_NO_WAIT) != 0 &&
		       blocks_sent < SEND_BLOCK_COUNT) {
			uint32_t block_size;
			int ret;

			ret = i2s_read(i2s_dev_rx, &mem_block, &block_size);
			if (ret < 0) {
				printk("i2s_read failed, ret: %d\n", ret);
				break;
			}

			for (int i = 0; i < SAMPLES_PER_BLOCK; ++i) {
				// Skip samples until something != 0 is received.
				if (!rx_mem_sample_is_set && ((int16_t *)mem_block)[i] == 0) {
					continue;
				}

				if (!rx_mem_sample_is_set) {
					rx_mem_sample = ((int16_t *)mem_block)[i];
					rx_mem_sample_is_set = true;
					printk("Initial rx_mem_sample was set to %d\n",
					       rx_mem_sample);
				}

				if (((int16_t *)mem_block)[i] != rx_mem_sample) {
					sample_mismatch_count++;
				}

				checked_sample_count++;
				rx_mem_sample++;
			}

			generate_block_data(mem_block, SAMPLES_PER_BLOCK);

			ret = i2s_write(i2s_dev_tx, mem_block, block_size);
			if (ret < 0) {
				printk("i2s_write failed, ret: %d\n", ret);
				break;
			}
			blocks_sent++;
		}

		printk("Checked sample count: %d\n", checked_sample_count);
		printk("Sample mismatch count: %d\n", sample_mismatch_count);

		if (!trigger_command(i2s_dev_rx, i2s_dev_tx, I2S_TRIGGER_DROP)) {
			return 0;
		}

		printk("Streams stopped\n");
	}
}
