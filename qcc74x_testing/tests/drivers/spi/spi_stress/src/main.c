/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <string.h>
#include <stdlib.h>

#define SPI_DEVICE_NODE DT_NODELABEL(spi0)
#define SPI_STRESS_ITERATIONS 100
#define CONCURRENT_ITERATIONS 100

#if CONFIG_NOCACHE_MEMORY
#define __NOCACHE __attribute__((__section__(".nocache")))
#elif defined(CONFIG_DT_DEFINED_NOCACHE)
#define __NOCACHE __attribute__((__section__(CONFIG_DT_DEFINED_NOCACHE_NAME)))
#else
#define __NOCACHE
#endif

static const struct device *spi_dev = DEVICE_DT_GET(SPI_DEVICE_NODE);

static const struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
};

/* DMA buffers must start on a cache-line boundary and be in non-cache memory. */
static uint8_t fifo_boundary_tx[65] __aligned(32) __NOCACHE;
static uint8_t fifo_boundary_rx[65] __aligned(32) __NOCACHE;
static uint8_t lsb_tx[32] __aligned(32) __NOCACHE;
static uint8_t lsb_rx[32] __aligned(32) __NOCACHE;

/* These buffers are shared by their respective ztest cases, not by workers. */
K_MUTEX_DEFINE(spi_test_buffer_lock);

/* One DMA-safe buffer pair per ztress worker. */
static uint8_t ztress_tx32[32] __aligned(32) __NOCACHE;
static uint8_t ztress_rx32[32] __aligned(32) __NOCACHE;
static uint8_t ztress_tx64[64] __aligned(32) __NOCACHE;
static uint8_t ztress_rx64[64] __aligned(32) __NOCACHE;
static uint8_t ztress_reconfig_tx[32] __aligned(32) __NOCACHE;
static uint8_t ztress_reconfig_rx[32] __aligned(32) __NOCACHE;

/* Helper to verify loopback data */
static int verify_loopback(const uint8_t *tx, const uint8_t *rx, size_t len)
{
	if (!rx || memcmp(tx, rx, len) != 0) {
		return -1;
	}
	return 0;
}

static void run_fifo_boundary_test(const struct spi_config *config)
{
	k_mutex_lock(&spi_test_buffer_lock, K_FOREVER);
	static const size_t lengths[] = { 7, 8, 9, 31, 32, 33, 63, 64, 65 };
	for (size_t i = 0; i < ARRAY_SIZE(lengths); i++) {
		size_t len = lengths[i];
		for (size_t j = 0; j < len; j++) {
			fifo_boundary_tx[j] = (uint8_t)(j ^ len);
		}
		struct spi_buf tx = { .buf = fifo_boundary_tx, .len = len };
		struct spi_buf rx = { .buf = fifo_boundary_rx, .len = len };
		struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
		struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
		int ret = spi_transceive(spi_dev, config, &tx_set, &rx_set);
		if (ret != 0) {
			k_mutex_unlock(&spi_test_buffer_lock);
			zassert_equal(ret, 0, "FIFO boundary %u failed", (unsigned int)len);
			return;
		}
		ret = verify_loopback(fifo_boundary_tx, fifo_boundary_rx, len);
		if (ret != 0) {
			k_mutex_unlock(&spi_test_buffer_lock);
			zassert_equal(ret, 0, "FIFO boundary %u data mismatch", (unsigned int)len);
			return;
		}
	}
	k_mutex_unlock(&spi_test_buffer_lock);
}

ZTEST(spi_stress_suite, test_spi_fifo_boundary_lengths)
{
	run_fifo_boundary_test(&spi_cfg);
}

ZTEST(spi_stress_suite, test_spi_lsb_first)
{
	k_mutex_lock(&spi_test_buffer_lock, K_FOREVER);
	for (size_t i = 0; i < sizeof(lsb_tx); i++) lsb_tx[i] = (uint8_t)(0xA5 ^ i);
	struct spi_config lsb_cfg = spi_cfg;
	/* spi_cfg is MSB-first; select LSB explicitly instead of setting both. */
	lsb_cfg.operation = (lsb_cfg.operation & ~SPI_TRANSFER_MSB) | SPI_TRANSFER_LSB;
	struct spi_buf tx = { .buf = lsb_tx, .len = sizeof(lsb_tx) };
	struct spi_buf rx = { .buf = lsb_rx, .len = sizeof(lsb_rx) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
	int ret = spi_transceive(spi_dev, &lsb_cfg, &tx_set, &rx_set);
	if (ret != 0) {
		k_mutex_unlock(&spi_test_buffer_lock);
		zassert_equal(ret, 0, "LSB-first transfer failed");
		return;
	}
	ret = verify_loopback(lsb_tx, lsb_rx, sizeof(lsb_tx));
	if (ret != 0) {
		k_mutex_unlock(&spi_test_buffer_lock);
		zassert_equal(ret, 0, "LSB-first data mismatch");
		return;
	}
	k_mutex_unlock(&spi_test_buffer_lock);
}

/* --- Scenario A: Single API Repeat --- */

static void *spi_stress_setup(void)
{
	zassert_true(device_is_ready(spi_dev), "SPI device is not ready");
	return NULL;
}

ZTEST_SUITE(spi_stress_suite, NULL, spi_stress_setup, NULL, NULL, NULL);

ZTEST(spi_stress_suite, test_spi_transceive_repeat)
{
	uint8_t tx_buf[32];
	uint8_t rx_buf[32];
	for (int i = 0; i < 32; i++) tx_buf[i] = i;

	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf rx_b = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set rx_set = { .buffers = &rx_b, .count = 1 };

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
		zassert_equal(ret, 0, "spi_transceive failed at iteration %d", i);
		zassert_equal(verify_loopback(tx_buf, rx_buf, sizeof(tx_buf)), 0, "Data mismatch at iteration %d", i);
	}
}

ZTEST(spi_stress_suite, test_spi_write_only_repeat)
{
	uint8_t tx_buf[32] = {0};
	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, NULL);
		zassert_equal(ret, 0, "spi_transceive write-only failed at iteration %d", i);
	}
}

ZTEST(spi_stress_suite, test_spi_read_only_repeat)
{
	uint8_t rx_buf[32];
	struct spi_buf rx = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		int ret = spi_transceive(spi_dev, &spi_cfg, NULL, &rx_set);
		zassert_equal(ret, 0, "spi_transceive read-only failed at iteration %d", i);
	}
}

/* --- Scenario B: Config/Release Cycle --- */

ZTEST(spi_stress_suite, test_spi_config_release_cycle)
{
	uint8_t tx_buf[16] = {0xAA};
	uint8_t rx_buf[16];
	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
	struct spi_config configs[2] = {
		spi_cfg,
		spi_cfg,
	};
	configs[0].frequency = 2000000;
	configs[1].frequency = 1000000;

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		const struct spi_config *config = &configs[i % 2];
		int ret = spi_transceive(spi_dev, config, &tx_set, &rx_set);
		zassert_equal(ret, 0, "Cycle failed at iteration %d", i);
	}
}

static const uint32_t freq_list[] = { 500000, 1000000, 2000000, 4000000, 8000000, 10000000, 12000000, 14000000, 20000000 };
ZTEST(spi_stress_suite, test_spi_reconfig_freq_cycle)
{
	uint8_t tx_buf[16] = {0xBB};
	uint8_t rx_buf[16];
	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
	struct spi_config configs[ARRAY_SIZE(freq_list)];

	for (size_t f = 0; f < ARRAY_SIZE(freq_list); f++) {
		configs[f] = spi_cfg;
		configs[f].frequency = freq_list[f];
	}

	for (size_t f = 0; f < ARRAY_SIZE(freq_list); f++) {
		for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
			int ret = spi_transceive(spi_dev, &configs[f], &tx_set, &rx_set);
			zassert_equal(ret, 0, "Freq %d failed at iteration %d", freq_list[f], i);
		}
	}
}

ZTEST(spi_stress_suite, test_spi_reconfig_mode_cycle)
{
	uint8_t tx_buf[16] = {0xCC};
	uint8_t rx_buf[16];
	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

	uint32_t modes[] = { SPI_OP_MODE_MASTER, SPI_OP_MODE_MASTER | SPI_MODE_CPOL, 
						  SPI_OP_MODE_MASTER | SPI_MODE_CPHA, SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA };
	struct spi_config configs[ARRAY_SIZE(modes)];

	for (size_t m = 0; m < ARRAY_SIZE(modes); m++) {
		configs[m] = spi_cfg;
		configs[m].operation = modes[m] | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
	}

	for (size_t m = 0; m < ARRAY_SIZE(modes); m++) {
		for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
			int ret = spi_transceive(spi_dev, &configs[m], &tx_set, &rx_set);
			zassert_equal(ret, 0, "Mode %d failed at iteration %d", m, i);
		}
	}
}

/* --- Scenario C: ztress Multi-threaded Concurrency --- */

/*
 * ztress owns the worker threads, their stacks, iteration counts and joins.
 * These workers intentionally share spi_dev. The QCC74x driver must hold its
 * spi_context lock from the start of each transfer through DMA/IRQ completion;
 * adding a test-side mutex would hide driver locking bugs.
 */
static bool ztress_transceive_32(void *user_data, uint32_t cnt,
					 bool last, int prio)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(last);
	ARG_UNUSED(prio);

	memset(ztress_tx32, 0x11, sizeof(ztress_tx32));
	struct spi_buf tx_buf = { .buf = ztress_tx32, .len = sizeof(ztress_tx32) };
	struct spi_buf rx_buf = { .buf = ztress_rx32, .len = sizeof(ztress_rx32) };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	zassert_equal(spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set), 0,
		      "32-byte concurrent transfer failed");
	zassert_equal(verify_loopback(ztress_tx32, ztress_rx32, sizeof(ztress_tx32)), 0,
		      "32-byte concurrent data mismatch");

	return true;
}

static bool ztress_transceive_64(void *user_data, uint32_t cnt,
					 bool last, int prio)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(last);
	ARG_UNUSED(prio);

	memset(ztress_tx64, 0x22, sizeof(ztress_tx64));
	struct spi_buf tx_buf = { .buf = ztress_tx64, .len = sizeof(ztress_tx64) };
	struct spi_buf rx_buf = { .buf = ztress_rx64, .len = sizeof(ztress_rx64) };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	zassert_equal(spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set), 0,
		      "64-byte concurrent transfer failed");
	zassert_equal(verify_loopback(ztress_tx64, ztress_rx64, sizeof(ztress_tx64)), 0,
		      "64-byte concurrent data mismatch");

	return true;
}

static bool ztress_reconfig_cycle(void *user_data, uint32_t cnt,
					 bool last, int prio)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(cnt);
	ARG_UNUSED(last);
	ARG_UNUSED(prio);

	/*
	 * Each call is synchronous and acquires/releases the driver's SPI context
	 * internally. No spi_release() is needed here because this test does not
	 * use spi_acquire(); it checks serialization across complete transfers
	 * while the configuration changes between transfers.
	 */
	struct spi_config config = spi_cfg;
	config.frequency = 500000;
	ztress_reconfig_tx[0] = 0x5A;
	struct spi_buf tx_buf = { .buf = ztress_reconfig_tx, .len = 1 };
	struct spi_buf rx_buf = { .buf = ztress_reconfig_rx, .len = 1 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	zassert_equal(spi_transceive(spi_dev, &config, &tx_set, &rx_set), 0,
		      "reconfiguration transfer failed");

	return true;
}

ZTEST(spi_stress_suite, test_spi_concurrent_transceive)
{
	ztress_set_timeout(K_SECONDS(10));
	ZTRESS_EXECUTE(
		ZTRESS_THREAD(ztress_transceive_32, NULL,
				      CONCURRENT_ITERATIONS, 0, Z_TIMEOUT_TICKS(10)),
		ZTRESS_THREAD(ztress_transceive_64, NULL,
				      CONCURRENT_ITERATIONS, 0, Z_TIMEOUT_TICKS(10))
	);
}

ZTEST(spi_stress_suite, test_spi_transceive_vs_reconfig)
{
	ztress_set_timeout(K_SECONDS(10));
	ZTRESS_EXECUTE(
		ZTRESS_THREAD(ztress_transceive_32, NULL,
				      CONCURRENT_ITERATIONS, 0, Z_TIMEOUT_TICKS(10)),
		ZTRESS_THREAD(ztress_reconfig_cycle, NULL,
				      CONCURRENT_ITERATIONS, 0, Z_TIMEOUT_TICKS(10))
	);
}

/* --- Scenario D: Boundary Conditions --- */

ZTEST(spi_stress_suite, test_spi_zero_length_repeat)
{
	/* Use zero-length buf_sets instead of NULL pointers;
	 * many SPI drivers reject NULL tx_set *and* NULL rx_set. */
	uint8_t tx_dummy;
	uint8_t rx_dummy;
	struct spi_buf tx_buf = { .buf = &tx_dummy, .len = 0 };
	struct spi_buf rx_buf = { .buf = &rx_dummy, .len = 0 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
		zassert_equal(ret, 0, "Zero length failed at iteration %d", i);
	}
}

ZTEST(spi_stress_suite, test_spi_max_length_repeat)
{
	uint8_t *tx_buf = k_malloc(8192);
	uint8_t *rx_buf = k_malloc(8192);
	if (tx_buf == NULL) {
		zassert_not_null(tx_buf, "TX malloc failed");
		return;
	}
	if (rx_buf == NULL) {
		k_free(tx_buf);
		zassert_not_null(rx_buf, "RX malloc failed");
		return;
	}
	memset(tx_buf, 0x55, 8192);

	struct spi_buf tx = { .buf = tx_buf, .len = 8192 };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = rx_buf, .len = 8192 };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
		if (ret != 0) {
			k_free(tx_buf);
			k_free(rx_buf);
			zassert_equal(ret, 0, "Max length failed at iteration %d", i);
			return;
		}
		int verify_ret = verify_loopback(tx_buf, rx_buf, 8192);
		if (verify_ret != 0) {
			k_free(tx_buf);
			k_free(rx_buf);
			zassert_equal(verify_ret, 0, "Data mismatch at iteration %d", i);
			return;
		}
	}
	k_free(tx_buf);
	k_free(rx_buf);
}

ZTEST(spi_stress_suite, test_spi_various_length_repeat)
{
	/* Allocate from heap instead of stack to avoid stack overflow
	 * (2×512 = 1024 bytes would exceed ztest thread stack). */
	uint8_t *tx_buf = k_malloc(512);
	uint8_t *rx_buf = k_malloc(512);

	zassert_not_null(tx_buf, "TX malloc failed");
	zassert_not_null(rx_buf, "RX malloc failed");

	for (int i = 0; i < SPI_STRESS_ITERATIONS; i++) {
		size_t len = (rand() % 512) + 1;
		memset(tx_buf, (uint8_t)(i % 256), len);

		struct spi_buf tx = { .buf = tx_buf, .len = len };
		struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
		struct spi_buf rx = { .buf = rx_buf, .len = len };
		struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
		zassert_equal(ret, 0, "Various length failed at iteration %d", i);
		zassert_equal(verify_loopback(tx_buf, rx_buf, len), 0, "Data mismatch at iteration %d", i);
	}
	k_free(tx_buf);
	k_free(rx_buf);
}


