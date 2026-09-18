/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * PSA Hash test suites for QCC74X Transparent Driver
 *
 * Test Suite  - psa_hash_stress:
 *   ZTRESS concurrent stress tests using the Zephyr ZTRESS framework.
 *   Works with both HW (PSA transparent driver) and SW fallback (mbedTLS).
 */

#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <psa/crypto.h>
#include <string.h>

/* ========================================================================
 * Test Vectors (NIST FIPS 180-4, input = "abc")
 * ======================================================================== */

#define STRESS_NUM_ALGS 7
#define EXHAUST_MAX_OPS 15

struct hash_stress_ctx {
	atomic_t success_cnt;
	atomic_t fail_cnt;
	atomic_t skip_cnt;
	atomic_t mem_fail_cnt;
	atomic_t verify_fail_cnt;
};


struct exhaust_ctx {
	struct hash_stress_ctx stats;
	psa_hash_operation_t ops[EXHAUST_MAX_OPS];
	atomic_t held_cnt;
};

static const uint8_t test_input[] = "abc";
static const size_t test_input_len = 3;

/* SHA-1("abc") */
static const uint8_t sha1_abc[] = {
	0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
	0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
	0x9c, 0xd0, 0xd8, 0x9d
};

static const uint8_t sha224_abc[] = {
	0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22,
	0x86, 0x42, 0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3,
	0x2a, 0xad, 0xbc, 0xe4, 0xbd, 0xa0, 0xb3, 0xf7,
	0xe3, 0x6c, 0x9d, 0xa7
};

static const uint8_t sha256_abc[] = {
	0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
	0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
	0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
	0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static const uint8_t sha384_abc[] = {
	0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b,
	0xb5, 0xa0, 0x3d, 0x69, 0x9a, 0xc6, 0x50, 0x07,
	0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
	0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed,
	0x80, 0x86, 0x07, 0x2b, 0xa1, 0xe7, 0xcc, 0x23,
	0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7
};

static const uint8_t sha512_abc[] = {
	0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
	0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
	0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
	0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
	0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
	0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
	0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
	0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
};

/* SHA-512/224("abc") - NIST FIPS 180-4 */
static const uint8_t sha512_224_abc[] = {
	0x46, 0x34, 0x27, 0x0f, 0x70, 0x7b, 0x6a, 0x54,
	0xda, 0xae, 0x75, 0x30, 0x46, 0x08, 0x42, 0xe2,
	0x0e, 0x37, 0xed, 0x26, 0x5c, 0xee, 0xe9, 0xa4,
	0x3e, 0x89, 0x24, 0xaa
};

/* SHA-512/256("abc") - NIST FIPS 180-4 */
static const uint8_t sha512_256_abc[] = {
	0x53, 0x04, 0x8e, 0x26, 0x81, 0x94, 0x1e, 0xf9,
	0x9b, 0x2e, 0x29, 0xb7, 0x6b, 0x4c, 0x7d, 0xab,
	0xe4, 0xc2, 0xd0, 0xc6, 0x34, 0xfc, 0x6d, 0x46,
	0xe0, 0xe2, 0xf1, 0x31, 0x07, 0xe7, 0xaf, 0x23
};

static const psa_algorithm_t stress_algs[] = {
	PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
	PSA_ALG_SHA_384, PSA_ALG_SHA_512,
	PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256
};

static const uint8_t *stress_expected[] = {
	sha1_abc, sha224_abc, sha256_abc, sha384_abc, sha512_abc,
	sha512_224_abc, sha512_256_abc
};

static const size_t stress_expected_lens[] = { 20, 28, 32, 48, 64, 28, 32 };

struct exhaust_ctx exhaust_ctx;
struct hash_stress_ctx oneshot_ctx;


 
/* ========================================================================
 * Suite setup
 * ======================================================================== */
static void *stress_suite_setup(void)
{
	psa_status_t status = psa_crypto_init();

	zassert_equal(status, PSA_SUCCESS, "psa_crypto_init failed: %d", status);
	printk("\n--- PSA Hash ZTRESS Stress Tests ---\n");
	printk("Build time: %s %s \n", __DATE__, __TIME__);
	printk("%s: %d, %s \n", __FILE__, __LINE__, __func__);
	printk("------------------------------------\n");
	return NULL;
}


static void stress_ctx_init(struct hash_stress_ctx *ctx)
{
	atomic_set(&ctx->success_cnt, 0);
	atomic_set(&ctx->fail_cnt, 0);
	atomic_set(&ctx->skip_cnt, 0);
	atomic_set(&ctx->mem_fail_cnt, 0);
	atomic_set(&ctx->verify_fail_cnt, 0);
}

static void stress_ctx_report(const char *test_name, const struct hash_stress_ctx *ctx)
{
	printk("%s: success=%ld, skip=%ld, mem_fail=%ld, verify_fail=%ld, fail=%ld\n",
	       test_name,
	       atomic_get(&ctx->success_cnt),
	       atomic_get(&ctx->skip_cnt),
	       atomic_get(&ctx->mem_fail_cnt),
	       atomic_get(&ctx->verify_fail_cnt),
	       atomic_get(&ctx->fail_cnt));
}

static void stress_ctx_assert(const struct hash_stress_ctx *ctx)
{
	zassert_equal(atomic_get(&ctx->verify_fail_cnt), 0,
		      "hash verification failed %ld times",
		      atomic_get(&ctx->verify_fail_cnt));
	zassert_equal(atomic_get(&ctx->fail_cnt), 0,
		      "unexpected PSA errors: %ld",
		      atomic_get(&ctx->fail_cnt));
	zassert_true(atomic_get(&ctx->success_cnt) > 0,
		     "no successful operations completed");
}

static void classify_oneshot_result(struct hash_stress_ctx *ctx,
				    psa_status_t s, int idx,
				    const uint8_t *hash, size_t hash_len)
{
	if (s == PSA_SUCCESS) {
		if (hash_len == stress_expected_lens[idx] &&
		    memcmp(hash, stress_expected[idx], hash_len) == 0) {
			atomic_inc(&ctx->success_cnt);
		} else {
			atomic_inc(&ctx->verify_fail_cnt);
		}
	} else if (s == PSA_ERROR_NOT_SUPPORTED) {
		atomic_inc(&ctx->skip_cnt);
	} else if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
		atomic_inc(&ctx->mem_fail_cnt);
	} else {
		atomic_inc(&ctx->fail_cnt);
	}
}

static bool stress_oneshot_handler(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct hash_stress_ctx *ctx = user_data;
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;
	int idx = sys_rand8_get() % STRESS_NUM_ALGS;

	psa_status_t s = psa_hash_compute(stress_algs[idx],
					  test_input, test_input_len,
					  hash, sizeof(hash), &hash_len);
	classify_oneshot_result(ctx, s, idx, hash, hash_len);
	return true;
}

static bool stress_multipart_handler(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct hash_stress_ctx *ctx = user_data;
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;
	int idx = sys_rand8_get() % STRESS_NUM_ALGS;
	psa_status_t s;

	s = psa_hash_setup(&op, stress_algs[idx]);
	if (s == PSA_ERROR_NOT_SUPPORTED) {
		atomic_inc(&ctx->skip_cnt);
		return true;
	}
	if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
		atomic_inc(&ctx->mem_fail_cnt);
		return true;
	}
	if (s != PSA_SUCCESS) {
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_update(&op, test_input, test_input_len);
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&op);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_finish(&op, hash, sizeof(hash), &hash_len);
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&op);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	if (hash_len == stress_expected_lens[idx] &&
	    memcmp(hash, stress_expected[idx], hash_len) == 0) {
		atomic_inc(&ctx->success_cnt);
	} else {
		atomic_inc(&ctx->verify_fail_cnt);
	}

	return true;
}

static bool stress_clone_handler(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct hash_stress_ctx *ctx = user_data;
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_hash_operation_t cloned = PSA_HASH_OPERATION_INIT;
	uint8_t hash1[PSA_HASH_MAX_SIZE], hash2[PSA_HASH_MAX_SIZE];
	size_t len1, len2;
	int idx = sys_rand8_get() % STRESS_NUM_ALGS;
	psa_status_t s;

	s = psa_hash_setup(&op, stress_algs[idx]);
	if (s == PSA_ERROR_NOT_SUPPORTED) {
		atomic_inc(&ctx->skip_cnt);
		return true;
	}
	if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
		atomic_inc(&ctx->mem_fail_cnt);
		return true;
	}
	if (s != PSA_SUCCESS) {
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_update(&op, test_input, test_input_len);
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&op);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_clone(&op, &cloned);
	if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
		psa_hash_abort(&op);
		atomic_inc(&ctx->mem_fail_cnt);
		return true;
	}
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&op);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_finish(&op, hash1, sizeof(hash1), &len1);
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&op);
		psa_hash_abort(&cloned);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	s = psa_hash_finish(&cloned, hash2, sizeof(hash2), &len2);
	if (s != PSA_SUCCESS) {
		psa_hash_abort(&cloned);
		atomic_inc(&ctx->fail_cnt);
		return true;
	}

	size_t exp_len = stress_expected_lens[idx];

	if (len1 == exp_len && len2 == exp_len &&
	    memcmp(hash1, stress_expected[idx], exp_len) == 0 &&
	    memcmp(hash2, stress_expected[idx], exp_len) == 0) {
		atomic_inc(&ctx->success_cnt);
	} else {
		atomic_inc(&ctx->verify_fail_cnt);
	}

	return true;
}

static bool stress_mixed_handler(void *user_data, uint32_t cnt, bool last, int prio)
{
	uint8_t op_type = sys_rand8_get() % 4;

	switch (op_type) {
	case 0:
		return stress_oneshot_handler(user_data, cnt, last, prio);
	case 1:
		return stress_multipart_handler(user_data, cnt, last, prio);
	case 2:
		return stress_clone_handler(user_data, cnt, last, prio);
	case 3: {
		struct hash_stress_ctx *ctx = user_data;
		psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
		int idx = sys_rand8_get() % STRESS_NUM_ALGS;
		psa_status_t s;

		s = psa_hash_setup(&op, stress_algs[idx]);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			atomic_inc(&ctx->skip_cnt);
			return true;
		}
		if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
			atomic_inc(&ctx->mem_fail_cnt);
			return true;
		}
		if (s != PSA_SUCCESS) {
			atomic_inc(&ctx->fail_cnt);
			return true;
		}

		psa_hash_update(&op, test_input, test_input_len);
		s = psa_hash_abort(&op);
		if (s == PSA_SUCCESS) {
			atomic_inc(&ctx->success_cnt);
		} else {
			atomic_inc(&ctx->fail_cnt);
		}
		return true;
	}
	default:
		return true;
	}
}

/* ========================================================================
 * Handler: session exhaustion (Scenario 5)
 * ======================================================================== */


static bool stress_exhaust_handler(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct exhaust_ctx *ctx = user_data;
	psa_status_t s;
	int held = atomic_get(&ctx->held_cnt);

	if (held < EXHAUST_MAX_OPS) {
		/* Try to accumulate sessions */
		ctx->ops[held] = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
		s = psa_hash_setup(&ctx->ops[held], PSA_ALG_SHA_256);
		if (s == PSA_SUCCESS) {
			psa_hash_update(&ctx->ops[held], test_input, test_input_len);
				atomic_inc(&ctx->held_cnt);
		} else if (s == PSA_ERROR_INSUFFICIENT_MEMORY) {
			atomic_inc(&ctx->stats.mem_fail_cnt);
		}
	} else {
		/* Release all held sessions */
		for (int i = 0; i < EXHAUST_MAX_OPS; i++) {
			uint8_t hash[PSA_HASH_MAX_SIZE];
			size_t hash_len;

			s = psa_hash_finish(&ctx->ops[i], hash, sizeof(hash), &hash_len);
			if (s == PSA_SUCCESS) {
				if (hash_len == 32 &&
				    memcmp(hash, sha256_abc, 32) == 0) {
					atomic_inc(&ctx->stats.success_cnt);
				} else {
					atomic_inc(&ctx->stats.verify_fail_cnt);
				}
			} else {
				psa_hash_abort(&ctx->ops[i]);
				atomic_inc(&ctx->stats.fail_cnt);
			}
		}
		atomic_set(&ctx->held_cnt, 0);
	}

	return true;
}

 /* ========================================================================
 * Test cases
 * ======================================================================== */

/* One-shot tests for function validation */
ZTEST(psa_hash_stress,  test_one_shot_sha1)
{
	uint8_t hash[20];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_1, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 20);
	zassert_mem_equal(hash, sha1_abc, 20);
}

ZTEST(psa_hash_stress,  test_one_shot_sha224)
{
	uint8_t hash[28];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_224, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 28);
	zassert_mem_equal(hash, sha224_abc, 28);
}

ZTEST(psa_hash_stress,  test_one_shot_sha256)
{
	uint8_t hash[32];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 32);
	zassert_mem_equal(hash, sha256_abc, 32);
}

ZTEST(psa_hash_stress,  test_one_shot_sha384)
{
	uint8_t hash[48];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_384, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 48);
	zassert_mem_equal(hash, sha384_abc, 48);
}

ZTEST(psa_hash_stress,  test_one_shot_sha512)
{
	uint8_t hash[64];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_512, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 64);
	zassert_mem_equal(hash, sha512_abc, 64);
}

ZTEST(psa_hash_stress,  test_one_shot_sha512_224)
{
	uint8_t hash[28];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_512_224, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 28);
	zassert_mem_equal(hash, sha512_224_abc, 28);
}

ZTEST(psa_hash_stress,  test_one_shot_sha512_256)
{
	uint8_t hash[32];
	size_t hash_len;

	zassert_equal(psa_hash_compute(PSA_ALG_SHA_512_256, test_input, test_input_len,
				       hash, sizeof(hash), &hash_len),
		      PSA_SUCCESS);
	zassert_equal(hash_len, 32);
	zassert_mem_equal(hash, sha512_256_abc, 32);
}


/* Multi-part tests */
ZTEST(psa_hash_stress,  test_multipart_sha1)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[20];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_1), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"a", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"b", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"c", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 20);
	zassert_mem_equal(hash, sha1_abc, 20);
}

ZTEST(psa_hash_stress,  test_multipart_sha256_split)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[32];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_256), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"a", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"b", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"c", 1), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 32);
	zassert_mem_equal(hash, sha256_abc, 32);
}

ZTEST(psa_hash_stress,  test_multipart_sha384)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[48];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_384), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 48);
	zassert_mem_equal(hash, sha384_abc, 48);
}

ZTEST(psa_hash_stress,  test_multipart_sha512)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[64];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_512), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 64);
	zassert_mem_equal(hash, sha512_abc, 64);
}

ZTEST(psa_hash_stress,  test_multipart_sha512_224)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[28];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_512_224), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 28);
	zassert_mem_equal(hash, sha512_224_abc, 28);
}

ZTEST(psa_hash_stress,  test_multipart_sha512_256)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t hash[32];
	size_t hash_len;

	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_512_256), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS);

	zassert_equal(hash_len, 32);
	zassert_mem_equal(hash, sha512_256_abc, 32);
}

/* Clone tests */

/* Basic clone correctness: setup → update → clone → both finish give same result */
ZTEST(psa_hash_stress,  test_clone_all_algs)
{
	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };
	const uint8_t *expected[] = { sha1_abc, sha224_abc, sha256_abc, sha384_abc, sha512_abc,
				      sha512_224_abc, sha512_256_abc };
	size_t expected_lens[] = { 20, 28, 32, 48, 64, 28, 32 };

	for (int a = 0; a < 7; a++) {
		psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
		psa_hash_operation_t cloned = PSA_HASH_OPERATION_INIT;
		uint8_t hash1[PSA_HASH_MAX_SIZE], hash2[PSA_HASH_MAX_SIZE];
		size_t len1, len2;
		size_t exp_len = expected_lens[a];
		psa_status_t s;

		s = psa_hash_setup(&op, algs[a]);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			printk("test_clone[%s]: SKIP (not supported by current backend)\n", names[a]);
			continue;
		}
		zassert_equal(s, PSA_SUCCESS, "[%s] setup failed", names[a]);
		zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS,
			      "[%s] update failed", names[a]);
		zassert_equal(psa_hash_clone(&op, &cloned), PSA_SUCCESS,
			      "[%s] clone failed", names[a]);
		zassert_equal(psa_hash_finish(&op, hash1, sizeof(hash1), &len1), PSA_SUCCESS,
			      "[%s] finish op failed", names[a]);
		zassert_equal(psa_hash_finish(&cloned, hash2, sizeof(hash2), &len2), PSA_SUCCESS,
			      "[%s] finish cloned failed", names[a]);

		zassert_equal(len1, exp_len, "[%s] len1 mismatch", names[a]);
		zassert_equal(len2, exp_len, "[%s] len2 mismatch", names[a]);
		zassert_mem_equal(hash1, expected[a], exp_len, "[%s] hash1 wrong", names[a]);
		zassert_mem_equal(hash2, expected[a], exp_len, "[%s] hash2 wrong", names[a]);

		printk("test_clone[%s]: PASS\n", names[a]);
	}
}

/* Independence test: clone diverges after different updates (SHA-256 representative) */
ZTEST(psa_hash_stress,  test_clone_independent)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_hash_operation_t cloned = PSA_HASH_OPERATION_INIT;
	uint8_t hash1[32], hash2[32];
	size_t len1, len2;

	/* Setup and feed partial data */
	zassert_equal(psa_hash_setup(&op, PSA_ALG_SHA_256), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&op, (uint8_t *)"a", 1), PSA_SUCCESS);

	/* Clone at this point */
	zassert_equal(psa_hash_clone(&op, &cloned), PSA_SUCCESS);

	/* Continue original with "bc", clone with "xy" → different results */
	zassert_equal(psa_hash_update(&op, (uint8_t *)"bc", 2), PSA_SUCCESS);
	zassert_equal(psa_hash_update(&cloned, (uint8_t *)"xy", 2), PSA_SUCCESS);

	printk("test: &hash1=%p, &hash2=%p\n", hash1, hash2);
	zassert_equal(psa_hash_finish(&op, hash1, sizeof(hash1), &len1), PSA_SUCCESS);
	zassert_equal(psa_hash_finish(&cloned, hash2, sizeof(hash2), &len2), PSA_SUCCESS);

	/* Debug: print actual hash values */
	printk("hash1 (actual)  : ");
	for (int i = 0; i < 32; i++) { printk("%02x", hash1[i]); }
	printk("\n");
	printk("sha256_abc (exp): ");
	for (int i = 0; i < 32; i++) { printk("%02x", sha256_abc[i]); }
	printk("\n");
	printk("hash2 (clone)   : ");
	for (int i = 0; i < 32; i++) { printk("%02x", hash2[i]); }
	printk("\n");

	/* hash1 = SHA-256("abc"), hash2 = SHA-256("axy") — must differ */
	zassert_mem_equal(hash1, sha256_abc, 32);
	zassert_true(memcmp(hash1, hash2, 32) != 0,
		     "Clone should produce independent result after different update");
}

/* ========================================================================
 * Abort and error tests
 * ======================================================================== */

ZTEST(psa_hash_stress,  test_abort_releases_session)
{
	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };

	for (int a = 0; a < 7; a++) {
		psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
		psa_status_t s;

		s = psa_hash_setup(&op, algs[a]);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			printk("test_abort[%s]: SKIP (not supported by current backend)\n", names[a]);
			continue;
		}
		zassert_equal(s, PSA_SUCCESS, "[%s] setup failed", names[a]);
		zassert_equal(psa_hash_update(&op, test_input, test_input_len), PSA_SUCCESS,
			      "[%s] update failed", names[a]);
		zassert_equal(psa_hash_abort(&op), PSA_SUCCESS,
			      "[%s] abort failed", names[a]);

		/* Double abort is no-op */
		zassert_equal(psa_hash_abort(&op), PSA_SUCCESS,
			      "[%s] double abort failed", names[a]);

		/* Can re-setup after abort */
		zassert_equal(psa_hash_setup(&op, algs[a]), PSA_SUCCESS,
			      "[%s] re-setup after abort failed", names[a]);
		psa_hash_abort(&op);

		printk("test_abort[%s]: PASS\n", names[a]);
	}
}

ZTEST(psa_hash_stress,  test_buffer_too_small)
{
	/* 10 bytes is smaller than all supported output sizes (min: SHA-1 = 20 bytes) */
	uint8_t hash[10];
	size_t hash_len;
	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };

	for (int a = 0; a < 7; a++) {
		psa_status_t s = psa_hash_compute(algs[a], test_input, test_input_len,
						  hash, sizeof(hash), &hash_len);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			printk("test_buffer_too_small[%s]: SKIP (not supported by current backend)\n",
			       names[a]);
			continue;
		}
		zassert_equal(s, PSA_ERROR_BUFFER_TOO_SMALL,
			      "[%s] expected BUFFER_TOO_SMALL", names[a]);
		printk("test_buffer_too_small[%s]: PASS\n", names[a]);
	}
}

ZTEST(psa_hash_stress,  test_unsupported_algorithm)
{
	uint8_t hash[64];
	size_t hash_len;

	psa_status_t status = psa_hash_compute(PSA_ALG_SHA3_256,
					       test_input, test_input_len,
					       hash, sizeof(hash), &hash_len);
	/* Should be NOT_SUPPORTED (hw) or SUCCESS (sw fallback) */
	zassert_true(status == PSA_ERROR_NOT_SUPPORTED || status == PSA_SUCCESS);
}

/* ========================================================================
 * Concurrent sessions
 * ======================================================================== */
ZTEST(psa_hash_stress,  test_concurrent_sessions)
{
	psa_hash_operation_t operations[10] = {0};
	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };
	const uint8_t *expected[] = { sha1_abc, sha224_abc, sha256_abc, sha384_abc, sha512_abc,
				      sha512_224_abc, sha512_256_abc };
	size_t expected_lens[] = { 20, 28, 32, 48, 64, 28, 32 };

	for (int a = 0; a < 7; a++) {
		
		uint8_t hash[PSA_HASH_MAX_SIZE];
		size_t hash_len;
		size_t exp_len = expected_lens[a];
		psa_status_t s;

		/* Check support before opening 10 sessions */
		operations[0] = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
		s = psa_hash_setup(&operations[0], algs[a]);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			printk("test_concurrent_sessions[%s]: SKIP (not supported by current backend)\n",
			       names[a]);
			continue;
		}
		zassert_equal(s, PSA_SUCCESS, "[%s] session 0 setup failed", names[a]);

		/* Open remaining 9 sessions */
		for (int i = 1; i < 10; i++) {
			operations[i] = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
			zassert_equal(psa_hash_setup(&operations[i], algs[a]), PSA_SUCCESS,
				      "[%s] session %d setup failed", names[a], i);
		}

		/* Interleave updates */
		for (int i = 0; i < 10; i++) {
			zassert_equal(psa_hash_update(&operations[i], test_input, test_input_len),
				      PSA_SUCCESS,
				      "[%s] session %d update failed", names[a], i);
		}

		/* Finish all and verify */
		for (int i = 0; i < 10; i++) {
			zassert_equal(psa_hash_finish(&operations[i], hash, sizeof(hash), &hash_len),
				      PSA_SUCCESS,
				      "[%s] session %d finish failed", names[a], i);
			zassert_equal(hash_len, exp_len,
				      "[%s] session %d hash_len mismatch", names[a], i);
			zassert_mem_equal(hash, expected[a], exp_len,
					  "[%s] session %d hash wrong", names[a], i);
		}

		printk("test_concurrent_sessions[%s]: PASS\n", names[a]);
	}
}

ZTEST(psa_hash_stress,  test_large_input)
{
	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };
	size_t expected_lens[] = { 20, 28, 32, 48, 64, 28, 32 };

		/* SHA-256 of 1MB of 'A' (0x41) - NIST pre-computed reference */
		static const uint8_t sha256_1mb_A[] = {
		0x4e, 0x29, 0xad, 0x18, 0xab, 0x9f, 0x42, 0xd7,
		0xc2, 0x33, 0x50, 0x07, 0x71, 0xa3, 0x9d, 0x7c,
		0x85, 0x2b, 0x20, 0x0b, 0xaf, 0x32, 0x8f, 0xd0,
		0x0f, 0xbb, 0xe3, 0xfe, 0xce, 0xa1, 0xeb, 0x56
	};

	uint8_t block[1024];
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;

	memset(block, 'A', sizeof(block));

	for (int a = 0; a < 7; a++) {
		psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
		psa_status_t s;

		s = psa_hash_setup(&op, algs[a]);
		if (s == PSA_ERROR_NOT_SUPPORTED) {
			printk("test_large_input[%s]: SKIP (not supported by current backend)\n",
			       names[a]);
			continue;
		}
		zassert_equal(s, PSA_SUCCESS, "[%s] setup failed", names[a]);

		/* 1024 x 1024 bytes = 1MB */
		for (int i = 0; i < 1024; i++) {
			zassert_equal(psa_hash_update(&op, block, sizeof(block)), PSA_SUCCESS,
				      "[%s] update %d failed", names[a], i);
		}

		zassert_equal(psa_hash_finish(&op, hash, sizeof(hash), &hash_len), PSA_SUCCESS,
			      "[%s] finish failed", names[a]);
		zassert_equal(hash_len, expected_lens[a],
			      "[%s] hash_len mismatch", names[a]);

		/* Verify hash value for SHA-256 (reference available) */
		if (algs[a] == PSA_ALG_SHA_256) {
			zassert_mem_equal(hash, sha256_1mb_A, 32,
					  "[SHA-256] 1MB hash value wrong");
		}

		printk("test_large_input[%s]: PASS (hash_len=%zu)\n", names[a], hash_len);
	}
}

/* ========================================================================
 * Timing tests (informational, always pass)
 * ======================================================================== */

ZTEST(psa_hash_stress,  test_timing_one_shot)
{
	uint8_t input[1024];
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;
	const int iterations = 50;
	uint32_t start = 0;
	uint32_t elapsed_us = 0;

	memset(input, 'B', sizeof(input));

	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,
				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };

	printk("\n--- Timing: one-shot (%d iterations x 1KB) ---\n", iterations);

	for (int a = 0; a < 7; a++) {
		for (int i = 0; i < iterations; i++) {
			start = k_cycle_get_32();
			psa_hash_compute(algs[a], input, sizeof(input),					 hash, sizeof(hash), &hash_len);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], [%d]: %u(us) \n", names[a], i, elapsed_us);
		}
	}
}

ZTEST(psa_hash_stress,  test_timing_multipart)
{
	uint8_t input[1024];
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;
	const int iterations = 50;
	uint32_t start = 0;
	uint32_t elapsed_us = 0;

	memset(input, 'C', sizeof(input));

	psa_algorithm_t algs[] = { PSA_ALG_SHA_1, PSA_ALG_SHA_224, PSA_ALG_SHA_256,				   PSA_ALG_SHA_384, PSA_ALG_SHA_512,
				   PSA_ALG_SHA_512_224, PSA_ALG_SHA_512_256 };
	const char *names[] = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
				"SHA-512/224", "SHA-512/256" };

	printk("\n--- Timing: multi-part (%d iterations x 4x256B) ---\n", iterations);

	for (int a = 0; a < 7; a++) {
		for (int i = 0; i < iterations; i++) {
			psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;

			start = k_cycle_get_32();
			psa_hash_setup(&op, algs[a]);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_setup[%d]: %u(us) \n", names[a], i, elapsed_us);

			start = k_cycle_get_32();
			psa_hash_update(&op, input, 256);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_update1[%d]: %u(us) \n", names[a], i, elapsed_us);

			start = k_cycle_get_32();
			psa_hash_update(&op, input + 256, 256);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_update2[%d]: %u(us) \n", names[a], i, elapsed_us);

			start = k_cycle_get_32();
			psa_hash_update(&op, input + 512, 256);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_update3[%d]: %u(us) \n", names[a], i, elapsed_us);

			start = k_cycle_get_32();
			psa_hash_update(&op, input + 768, 256);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_update4[%d]: %u(us) \n", names[a], i, elapsed_us);

			start = k_cycle_get_32();
			psa_hash_finish(&op, hash, sizeof(hash), &hash_len);
			elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			printk("TC[%s], psa_hash_finish[%d]: %u(us) \n", names[a], i, elapsed_us);
		}
	}
}


 



ZTEST(psa_hash_stress, test_stress_oneshot)
{
	struct hash_stress_ctx ctx;

	stress_ctx_init(&ctx);
	ztress_set_timeout(K_MSEC(5000));

	ZTRESS_EXECUTE(
		ZTRESS_THREAD(stress_oneshot_handler, &ctx, 0, 0,    Z_TIMEOUT_TICKS(20)),
		ZTRESS_THREAD(stress_oneshot_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(20)),
		ZTRESS_THREAD(stress_oneshot_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(20))
	);

	stress_ctx_report("stress_oneshot", &ctx);
	stress_ctx_assert(&ctx);
}

ZTEST(psa_hash_stress, test_stress_multipart)
{
	struct hash_stress_ctx ctx;

	stress_ctx_init(&ctx);
	ztress_set_timeout(K_MSEC(5000));

	ZTRESS_EXECUTE(
		ZTRESS_THREAD(stress_multipart_handler, &ctx, 0, 0,    Z_TIMEOUT_TICKS(30)),
		ZTRESS_THREAD(stress_multipart_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(30)),
		ZTRESS_THREAD(stress_multipart_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(30))
	);

	stress_ctx_report("stress_multipart", &ctx);
	stress_ctx_assert(&ctx);
}

ZTEST(psa_hash_stress, test_stress_clone)
{
	struct hash_stress_ctx ctx;

	stress_ctx_init(&ctx);
	ztress_set_timeout(K_MSEC(5000));

	ZTRESS_EXECUTE(
		ZTRESS_THREAD(stress_clone_handler, &ctx, 0, 0,    Z_TIMEOUT_TICKS(30)),
		ZTRESS_THREAD(stress_clone_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(30))
	);

	stress_ctx_report("stress_clone", &ctx);
	stress_ctx_assert(&ctx);
}

ZTEST(psa_hash_stress, test_stress_mixed)
{
	struct hash_stress_ctx ctx;

	stress_ctx_init(&ctx);
	ztress_set_timeout(K_MSEC(10000));

	ZTRESS_EXECUTE(
		ZTRESS_THREAD(stress_mixed_handler, &ctx, 0, 0,    Z_TIMEOUT_TICKS(30)),
		ZTRESS_THREAD(stress_mixed_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(30)),
		ZTRESS_THREAD(stress_mixed_handler, &ctx, 0, 2000, Z_TIMEOUT_TICKS(30))
	);

	stress_ctx_report("stress_mixed", &ctx);
	stress_ctx_assert(&ctx);
}

ZTEST(psa_hash_stress, test_stress_exhaust)
{
	memset(&exhaust_ctx, 0, sizeof(exhaust_ctx));
	stress_ctx_init(&exhaust_ctx.stats);
	stress_ctx_init(&oneshot_ctx);
	ztress_set_timeout(K_MSEC(5000));

	ZTRESS_EXECUTE(
		ZTRESS_THREAD(stress_exhaust_handler, &exhaust_ctx, 0, 0,    Z_TIMEOUT_TICKS(20)),
	  ZTRESS_THREAD(stress_oneshot_handler, &oneshot_ctx, 0, 2000, Z_TIMEOUT_TICKS(20))
	);

	printk("stress_exhaust (exhaust thread): ");
	stress_ctx_report("  exhaust", &exhaust_ctx.stats);
	printk("stress_exhaust (oneshot thread): ");
	stress_ctx_report("  oneshot", &oneshot_ctx);

	/* For exhaust test: verify_fail and fail must be 0, but mem_fail is expected */
	zassert_equal(atomic_get(&exhaust_ctx.stats.verify_fail_cnt), 0,
		      "exhaust: hash verification failed");
	zassert_equal(atomic_get(&exhaust_ctx.stats.fail_cnt), 0,
		      "exhaust: unexpected errors");
	zassert_equal(atomic_get(&oneshot_ctx.verify_fail_cnt), 0,
		      "oneshot: hash verification failed");
	zassert_equal(atomic_get(&oneshot_ctx.fail_cnt), 0,
		      "oneshot: unexpected errors");
}

/* ========================================================================
 * Suite definition
 * ======================================================================== */

ZTEST_SUITE(psa_hash_stress, NULL, stress_suite_setup, NULL, NULL, NULL);
