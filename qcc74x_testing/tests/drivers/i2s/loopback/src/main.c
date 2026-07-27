/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define I2S_RXTX_NODE DT_ALIAS(i2s_rxtx)

#define SAMPLE_FREQUENCY   44100
#define SAMPLE_BIT_WIDTH   16
#define BYTES_PER_SAMPLE   sizeof(int16_t)
#define NUMBER_OF_CHANNELS 2
#define SAMPLES_PER_BLOCK  ((SAMPLE_FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS     2
#define TIMEOUT            1000

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + 2)

#if defined(CONFIG_DCACHE_LINE_SIZE) && (CONFIG_DCACHE_LINE_SIZE > 0)
#define DCACHE_LINE_SIZE CONFIG_DCACHE_LINE_SIZE
#else
#define DCACHE_LINE_SIZE 32
#endif
// Align start of each slab block to data cache line size.
K_MEM_SLAB_DEFINE_STATIC(mem_slab, (BLOCK_SIZE + DCACHE_LINE_SIZE - 1) & ~(DCACHE_LINE_SIZE - 1),
			 BLOCK_COUNT, DCACHE_LINE_SIZE);

#define SEND_BLOCK_COUNT            10U
#define ALLOWED_TX_RX_SAMPLE_OFFSET 10U

static int16_t tx_mem_sample;

/**
 * @brief Generate block data with sample values.
 *
 * Sample values increase by 1 by every sample. Sample values can wrap around.
 */
static void generate_block_data(void *mem_block, uint32_t number_of_samples)
{
	for (uint32_t i = 0; i < number_of_samples; i++) {
		((int16_t *)mem_block)[i] = ++tx_mem_sample;
	}
}

static void prepare_transfer(const struct device *i2s_dev_tx)
{
	for (int i = 0; i < INITIAL_BLOCKS; i++) {
		void *mem_block;
		int ret = k_mem_slab_alloc(&mem_slab, &mem_block, K_NO_WAIT);

		zassert_ok(ret, "allocate TX block %d failed with code %d", i, ret);
		generate_block_data(mem_block, SAMPLES_PER_BLOCK);

		ret = i2s_write(i2s_dev_tx, mem_block, BLOCK_SIZE);
		zassert_ok(ret, "write block %d failed with code %d", i, ret);
	}
}

ZTEST(i2s_loopback, test_loopback_transfer)
{
	const struct device *const i2s_dev_rxtx = DEVICE_DT_GET(I2S_RXTX_NODE);
	struct i2s_config config = {
		.word_size = SAMPLE_BIT_WIDTH,
		.channels = NUMBER_OF_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = SAMPLE_FREQUENCY,
		.mem_slab = &mem_slab,
		.block_size = BLOCK_SIZE,
		.timeout = TIMEOUT,
	};
	int blocks_sent = 0;
	int checked_sample_count = 0;
	int sample_mismatch_count = 0;
	int16_t expected_rx_sample = 0;
	bool expected_rx_sample_is_set = false;
	int ret;

	zassert_true(device_is_ready(i2s_dev_rxtx), "%s is not ready", i2s_dev_rxtx->name);

	tx_mem_sample = 0;
	ret = i2s_configure(i2s_dev_rxtx, I2S_DIR_BOTH, &config);
	zassert_ok(ret, "configure both streams failed with code %d", ret);
	prepare_transfer(i2s_dev_rxtx);
	ret = i2s_trigger(i2s_dev_rxtx, I2S_DIR_RX, I2S_TRIGGER_START);
	zassert_ok(ret, "trigger RX stream with cmd I2S_TRIGGER_START failed with code %d", ret);
	ret = i2s_trigger(i2s_dev_rxtx, I2S_DIR_TX, I2S_TRIGGER_START);
	zassert_ok(ret, "trigger TX stream with cmd I2S_TRIGGER_START failed with code %d", ret);

	while (blocks_sent < SEND_BLOCK_COUNT) {
		void *mem_block;
		uint32_t block_size;
		ret = i2s_read(i2s_dev_rxtx, &mem_block, &block_size);

		zassert_ok(ret, "i2s_read failed with code %d", ret);
		zassert_equal(block_size, BLOCK_SIZE,
			      "read block size mismatch: expected %u got %u", BLOCK_SIZE,
			      block_size);

		for (uint32_t i = 0; i < SAMPLES_PER_BLOCK; i++) {
			int16_t sample = ((int16_t *)mem_block)[i];

			/* Skip received samples until 1 is received. This is to allow offset
			 * between TX and RX due to GPIO loopback delay. */
			if (!expected_rx_sample_is_set && sample != 1) {
				continue;
			}

			if (!expected_rx_sample_is_set) {
				expected_rx_sample = sample;
				expected_rx_sample_is_set = true;
			}

			if (sample != expected_rx_sample) {
				sample_mismatch_count++;
			}

			checked_sample_count++;
			expected_rx_sample++;
		}

		generate_block_data(mem_block, SAMPLES_PER_BLOCK);
		ret = i2s_write(i2s_dev_rxtx, mem_block, block_size);
		zassert_ok(ret, "i2s_write failed with code %d", ret);
		blocks_sent++;
	}

	ret = i2s_trigger(i2s_dev_rxtx, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	zassert_ok(ret, "trigger TX stream with cmd %d failed with code %d", I2S_TRIGGER_DRAIN,
		   ret);

	ret = i2s_trigger(i2s_dev_rxtx, I2S_DIR_RX, I2S_TRIGGER_STOP);
	zassert_ok(ret, "trigger RX stream with cmd %d failed with code %d", I2S_TRIGGER_STOP, ret);

	TC_PRINT("Checked sample count: %d\n", checked_sample_count);
	TC_PRINT("Total sample count: %d\n", SEND_BLOCK_COUNT * SAMPLES_PER_BLOCK);
	zassert_true(checked_sample_count >=
			     (SEND_BLOCK_COUNT * SAMPLES_PER_BLOCK - ALLOWED_TX_RX_SAMPLE_OFFSET),
		     "not enough samples were checked");
	zassert_true(sample_mismatch_count == 0, "%d samples mismatched", sample_mismatch_count);
}

ZTEST_SUITE(i2s_loopback, NULL, NULL, NULL, NULL, NULL);
