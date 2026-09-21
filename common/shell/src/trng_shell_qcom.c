/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * QCC74x TRNG / PSA random Shell Commands
 *
 * Test commands to verify TRNG HW acceleration through
 * Zephyr entropy bridge:
 *
 *   psa_generate_random() -> mbedtls_ctr_drbg_random()
 *     -> zephyr_entropy.c -> entropy_qcc74x.c -> qc7xx_trng_read()
 *
 * Shell commands:
 *   trng info              -- Show TRNG chain info
 *   trng get <len>         -- Get random bytes via psa_generate_random()
 *   trng bench [kb]        -- Benchmark TRNG throughput
 *   trng selftest          -- Run basic TRNG sanity tests
 *   trng entropy           -- Direct entropy_get_entropy() test
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/devicetree.h>
#include <psa/crypto.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Helper: print HW acceleration status based on compile-time checks.
 * We check whether a Zephyr entropy device is configured and the DTS
 * zephyr,entropy chosen node exists. This tells us if PSA random is
 * going through the entropy_qcc74x.c driver.
 */
#if defined(CONFIG_ENTROPY_HAS_DRIVER) && DT_NODE_EXISTS(DT_CHOSEN(zephyr_entropy))
#define TRNG_HW_ACCEL_STR  "ENABLED (via Zephyr entropy device)"
#else
#define TRNG_HW_ACCEL_STR  "DISABLED (software only, no entropy driver)"
#endif

/* =========================================================================
 *  TRNG info
 * ======================================================================= */
static int cmd_trng_info(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "QCC74x TRNG / PSA Random Info:");
	shell_print(sh, "  PSA API:      psa_generate_random()");

#if defined(CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY)
	shell_print(sh, "  Entropy bridge: CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY (enabled)");
	shell_print(sh, "  Bridge path:   zephyr_entropy.c -> entropy_get_entropy()");
#else
	shell_print(sh, "  Entropy bridge: DISABLED");
#endif

#if defined(CONFIG_ENTROPY_HAS_DRIVER)
#if DT_NODE_EXISTS(DT_CHOSEN(zephyr_entropy))
	shell_print(sh, "  zephyr,entropy: (DT defined, node present)");
#else
	shell_print(sh, "  zephyr,entropy: (NOT defined or node not enabled)");
#endif
#else
	shell_print(sh, "  Entropy dev:   NONE (no entropy driver)");
#endif

#if defined(CONFIG_ENTROPY_QCC74X)
	shell_print(sh, "  QCC74x TRNG:   CONFIG_ENTROPY_QCC74X (enabled)");
#else
	shell_print(sh, "  QCC74x TRNG:   DISABLED");
#endif

#if defined(CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY) && \
    defined(CONFIG_ENTROPY_HAS_DRIVER) && \
    DT_NODE_EXISTS(DT_CHOSEN(zephyr_entropy)) && \
    defined(CONFIG_ENTROPY_QCC74X)
	shell_print(sh, "  Chain status:  COMPLETE (PSA -> mbedTLS -> Zephyr entropy -> QCC74x HW)");
#else
	shell_print(sh, "  Chain status:  INCOMPLETE (see above)");
#endif

	shell_print(sh, "  HW path:      SEC_ENG TRNG -> qc7xx_trng_read()");
	shell_print(sh, "  HW accel:     " TRNG_HW_ACCEL_STR);

	return 0;
}

/* =========================================================================
 *  trng get <len>  -- get random bytes via PSA
 * ======================================================================= */
static int cmd_trng_get(const struct shell *sh, size_t argc, char **argv)
{
	long len = 32;

	if (argc > 1) {
		len = shell_strtol(argv[1], 10, NULL);
		if (len <= 0 || len > 4096) {
			shell_error(sh, "Length must be 1..4096");
			return -EINVAL;
		}
	}

	uint8_t *buf = (uint8_t *)k_malloc((size_t)len);
	if (!buf) {
		shell_error(sh, "Failed to allocate %ld bytes", len);
		return -ENOMEM;
	}

	psa_status_t status = psa_generate_random(buf, (size_t)len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_generate_random() failed: %d", status);
		k_free(buf);
		return -EIO;
	}

	shell_print(sh, "Random bytes (%ld):", len);
	shell_hexdump(sh, buf, (size_t)len);

	k_free(buf);
	return 0;
}

/* =========================================================================
 *  trng bench [kb]  -- benchmark TRNG throughput
 * ======================================================================= */
static int cmd_trng_bench(const struct shell *sh, size_t argc, char **argv)
{
	long total_kb = 1; /* default: 1 KB */

	if (argc > 1) {
		total_kb = shell_strtol(argv[1], 10, NULL);
		if (total_kb <= 0 || total_kb > 1024) {
			shell_error(sh, "Payload must be 1..1024 KB");
			return -EINVAL;
		}
	}

	size_t total_bytes = (size_t)total_kb * 1024;
	size_t chunk = 256; /* 256 bytes per call */
	uint8_t *buf = (uint8_t *)k_malloc(chunk);
	if (!buf) {
		shell_error(sh, "Failed to allocate buffer");
		return -ENOMEM;
	}

	int64_t start_us = k_ticks_to_us_floor64(k_uptime_ticks());

	size_t remaining = total_bytes;
	while (remaining > 0) {
		size_t this_chunk = (remaining < chunk) ? remaining : chunk;
		psa_status_t status = psa_generate_random(buf, this_chunk);
		if (status != PSA_SUCCESS) {
			shell_error(sh, "psa_generate_random() failed at offset %zu: %d",
				    total_bytes - remaining, status);
			k_free(buf);
			return -EIO;
		}
		remaining -= this_chunk;
	}

	int64_t end_us = k_ticks_to_us_floor64(k_uptime_ticks());
	int64_t elapsed_us = end_us - start_us;
	if (elapsed_us <= 0) elapsed_us = 1;

	uint32_t throughput_bps = (uint32_t)((uint64_t)total_bytes * 1000000ULL
					      / (uint64_t)elapsed_us);
	uint32_t throughput_kbps = throughput_bps / 1024;

	shell_print(sh, "TRNG Benchmark:");
	shell_print(sh, "  HW accel:     " TRNG_HW_ACCEL_STR);
	shell_print(sh, "  Total data : %ld KB (%zu bytes)", total_kb, total_bytes);
	shell_print(sh, "  Chunk size : %zu bytes", chunk);
	shell_print(sh, "  Elapsed    : %lld us", (long long)elapsed_us);
	shell_print(sh, "  Throughput : %u B/s  (%u KB/s)", throughput_bps, throughput_kbps);
	shell_print(sh, "  API        : psa_generate_random()");

	k_free(buf);
	return 0;
}

/* =========================================================================
 *  trng selftest  -- basic TRNG sanity tests
 * ======================================================================= */
static int cmd_trng_selftest(const struct shell *sh, size_t argc, char **argv)
{
	int passed = 0;
	int failed = 0;
	psa_status_t status;
	uint8_t buf1[32], buf2[32], buf3[128];
	bool ok;
	bool ok1, ok2;

	shell_print(sh, "TRNG Selftest:");
	shell_print(sh, "");
	shell_print(sh, "  HW accel:     " TRNG_HW_ACCEL_STR);
	shell_print(sh, "");

	/* Test 1: basic PSA random */
	status = psa_generate_random(buf1, sizeof(buf1));
	ok = (status == PSA_SUCCESS);
	shell_print(sh, "  psa_generate_random(32) ... %s",
		    ok ? "PASS" : "FAIL");
	if (ok) passed++; else failed++;

	/* Test 2: larger buffer */
	status = psa_generate_random(buf3, sizeof(buf3));
	ok = (status == PSA_SUCCESS);
	shell_print(sh, "  psa_generate_random(128) ... %s",
		    ok ? "PASS" : "FAIL");
	if (ok) passed++; else failed++;

	/* Test 3: consecutive calls produce different output */
	status = psa_generate_random(buf1, sizeof(buf1));
	ok1 = (status == PSA_SUCCESS);
	status = psa_generate_random(buf2, sizeof(buf2));
	ok2 = (status == PSA_SUCCESS);
	ok = ok1 && ok2 && (memcmp(buf1, buf2, sizeof(buf1)) != 0);
	shell_print(sh, "  consecutive randomness test ... %s",
		    ok ? "PASS" : "FAIL");
	if (ok) passed++; else failed++;

	/* Test 4: small 1-byte read */
	uint8_t single;
	status = psa_generate_random(&single, 1);
	ok = (status == PSA_SUCCESS);
	shell_print(sh, "  psa_generate_random(1) ... %s",
		    ok ? "PASS" : "FAIL");
	if (ok) passed++; else failed++;

	shell_print(sh, "Selftest: %d passed, %d failed", passed, failed);

	/* Display first 32 bytes for sanity check */
	shell_print(sh, "Sample output (first 32 bytes):");
	shell_hexdump(sh, buf1, sizeof(buf1));

	return (failed == 0) ? 0 : -EIO;
}

/* =========================================================================
 *  trng entropy  -- Direct entropy_get_entropy() test
 * ======================================================================= */
static int cmd_trng_entropy(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_ENTROPY_HAS_DRIVER)
	const struct device *entropy_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));

	if (!device_is_ready(entropy_dev)) {
		shell_error(sh, "Entropy device not ready. "
			    "Check that zephyr,entropy points to a TRNG node with status=okay "
			    "and CONFIG_ENTROPY_QCC74X is enabled.");
		return -ENODEV;
	}

	shell_print(sh, "Entropy device ready, calling entropy_get_entropy()...");
	shell_print(sh, "  HW accel:     " TRNG_HW_ACCEL_STR);

	uint8_t buf[32];
	int ret = entropy_get_entropy(entropy_dev, buf, sizeof(buf));
	if (ret != 0) {
		shell_error(sh, "entropy_get_entropy() failed: %d", ret);
		return -EIO;
	}

	shell_print(sh, "entropy_get_entropy(32 bytes):");
	shell_hexdump(sh, buf, sizeof(buf));
	return 0;
#else
	shell_error(sh, "CONFIG_ENTROPY_HAS_DRIVER not enabled. "
		    "Ensure CONFIG_ENTROPY_QCC74X is set in prj.conf.");
	return -ENOTSUP;
#endif
}

/* =========================================================================
 *  Shell command registration
 * ======================================================================= */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_trng_commands,
	SHELL_CMD_ARG(info, NULL,
		      "Display TRNG / PSA random chain info.\n"
		      "Usage: trng info\n",
		      cmd_trng_info, 1, 0),

	SHELL_CMD_ARG(get, NULL,
		      "Get random bytes via psa_generate_random().\n"
		      "Usage: trng get [len]\n"
		      "  len: number of bytes (default 32, max 4096)\n",
		      cmd_trng_get, 1, 1),

	SHELL_CMD_ARG(bench, NULL,
		      "Benchmark TRNG throughput via psa_generate_random().\n"
		      "Usage: trng bench [kb]\n"
		      "  kb: payload size in KB (default 1, max 1024)\n",
		      cmd_trng_bench, 1, 1),

	SHELL_CMD_ARG(selftest, NULL,
		      "Run basic TRNG sanity tests.\n"
		      "Usage: trng selftest\n",
		      cmd_trng_selftest, 1, 0),

	SHELL_CMD_ARG(entropy, NULL,
		      "Test entropy_get_entropy() directly.\n"
		      "Usage: trng entropy\n",
		      cmd_trng_entropy, 1, 0),

	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(trng, &sub_trng_commands,
		   "QCC74x TRNG / PSA random commands (get/bench/selftest/info/entropy)",
		   NULL);