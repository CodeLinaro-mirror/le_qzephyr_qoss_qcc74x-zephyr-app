/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * qs_its_shell.c - PSA ITS (Internal Trusted Storage) shell commands
 *
 * This module provides interactive shell commands for testing
 * PSA ITS operations:
 *   its set <uid> <hex_data>       - Write data
 *   its get <uid>                  - Read data
 *   its get_info <uid>             - Query metadata
 *   its remove <uid>               - Delete data
 *   its set_once <uid> <hex_data>  - Write once (cannot overwrite)
 *   its stress <count>             - Run stress test
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <psa/internal_trusted_storage.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(qs_its_shell, CONFIG_SECURE_STORAGE_LOG_LEVEL);

/* ================================================================
 * Helper: convert hex string to binary
 * e.g. "48656C6C6F" -> {0x48, 0x65, 0x6C, 0x6C, 0x6F}
 * ================================================================ */
static int hex2bin_simple(const char *hex, size_t hex_len,
			   uint8_t *bin, size_t bin_size)
{
	size_t bin_len = hex_len / 2;
	size_t i;

	if (bin_len > bin_size) {
		return -ENOMEM;
	}

	for (i = 0; i < bin_len; i++) {
		char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
		bin[i] = (uint8_t)strtol(byte_str, NULL, 16);
	}
	return (int)bin_len;
}

/* ================================================================
 * its set <uid> <hex_data>
 * ================================================================ */
static int cmd_its_set(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid;
	uint8_t data[CONFIG_SECURE_STORAGE_ITS_MAX_DATA_SIZE];
	ssize_t data_len;
	psa_status_t ret;

	if (argc < 3) {
		shell_error(sh, "Usage: its set <uid> <hex_data>");
		return -EINVAL;
	}

	uid = strtoul(argv[1], NULL, 0);
	data_len = hex2bin_simple(argv[2], strlen(argv[2]), data, sizeof(data));
	if (data_len < 0) {
		shell_error(sh, "Invalid hex data or too long (max %zu bytes)",
			    sizeof(data));
		return -EINVAL;
	}

	ret = psa_its_set(uid, data_len, data, 0);
	shell_print(sh, "psa_its_set(%u, %zd bytes) = %d", uid, data_len, ret);
	return 0;
}

/* ================================================================
 * its get <uid>
 * ================================================================ */
static int cmd_its_get(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid;
	uint8_t buf[CONFIG_SECURE_STORAGE_ITS_MAX_DATA_SIZE];
	size_t data_len;
	psa_status_t ret;

	if (argc < 2) {
		shell_error(sh, "Usage: its get <uid>");
		return -EINVAL;
	}

	uid = strtoul(argv[1], NULL, 0);
	ret = psa_its_get(uid, 0, sizeof(buf), buf, &data_len);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "psa_its_get(%u) = %d", uid, ret);
		return 0;
	}

	shell_print(sh, "Data (%zu bytes):", data_len);
	shell_hexdump(sh, buf, data_len);
	return 0;
}

/* ================================================================
 * its get_info <uid>
 * ================================================================ */
static int cmd_its_get_info(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid;
	struct psa_storage_info_t info;
	psa_status_t ret;

	if (argc < 2) {
		shell_error(sh, "Usage: its get_info <uid>");
		return -EINVAL;
	}

	uid = strtoul(argv[1], NULL, 0);
	ret = psa_its_get_info(uid, &info);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "psa_its_get_info(%u) = %d", uid, ret);
		return 0;
	}

	shell_print(sh, "UID %u: size=%zu, flags=0x%x",
		    uid, info.size, info.flags);
	return 0;
}

/* ================================================================
 * its remove <uid>
 * ================================================================ */
static int cmd_its_remove(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid;
	psa_status_t ret;

	if (argc < 2) {
		shell_error(sh, "Usage: its remove <uid>");
		return -EINVAL;
	}

	uid = strtoul(argv[1], NULL, 0);
	ret = psa_its_remove(uid);
	shell_print(sh, "psa_its_remove(%u) = %d", uid, ret);
	return 0;
}

/* ================================================================
 * its set_once <uid> <hex_data>  (PSA_STORAGE_FLAG_WRITE_ONCE)
 * ================================================================ */
static int cmd_its_set_once(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid;
	uint8_t data[CONFIG_SECURE_STORAGE_ITS_MAX_DATA_SIZE];
	ssize_t data_len;
	psa_status_t ret;

	if (argc < 3) {
		shell_error(sh, "Usage: its set_once <uid> <hex_data>");
		return -EINVAL;
	}

	uid = strtoul(argv[1], NULL, 0);
	data_len = hex2bin_simple(argv[2], strlen(argv[2]), data, sizeof(data));
	if (data_len < 0) {
		shell_error(sh, "Invalid hex data or too long (max %zu bytes)",
			    sizeof(data));
		return -EINVAL;
	}

	ret = psa_its_set(uid, data_len, data, PSA_STORAGE_FLAG_WRITE_ONCE);
	shell_print(sh, "psa_its_set(%u, %zd, WRITE_ONCE) = %d",
		    uid, data_len, ret);
	return 0;
}

/* ================================================================
 * its stress <count>
 *
 * Runs a simple stress test: write 10 different UIDs, verify each,
 * then remove them. Repeats <count> times.
 * ================================================================ */
static int cmd_its_stress(const struct shell *sh, size_t argc, char **argv)
{
	long count = 10;
	psa_status_t ret;
	int passed = 0, failed = 0;

	if (argc >= 2) {
		count = strtol(argv[1], NULL, 0);
		if (count <= 0) {
			shell_error(sh, "Invalid count: %ld", count);
			return -EINVAL;
		}
	}

	shell_print(sh, "ITS Stress Test: %ld iterations...", count);

	for (long iter = 0; iter < count; iter++) {
		/* Write 10 UIDs */
		for (psa_storage_uid_t uid = 1; uid <= 10; uid++) {
			uint8_t data[8];
			uint8_t buf[8];
			size_t len;

			/* Data = iteration + uid */
			data[0] = (uint8_t)(iter & 0xFF);
			data[1] = (uint8_t)((iter >> 8) & 0xFF);
			data[2] = (uint8_t)(uid & 0xFF);
			data[3] = (uint8_t)((uid >> 8) & 0xFF);
			memset(data + 4, 0xAB, 4);

			ret = psa_its_set(uid, sizeof(data), data, 0);
			if (ret != PSA_SUCCESS) {
				shell_print(sh, "  Iter %ld, UID %u: SET FAILED (%d)",
					    iter, uid, ret);
				failed++;
				continue;
			}

			ret = psa_its_get(uid, 0, sizeof(buf), buf, &len);
			if (ret != PSA_SUCCESS || len != sizeof(data) ||
			    memcmp(buf, data, len) != 0) {
				shell_print(sh, "  Iter %ld, UID %u: GET FAILED (%d)",
					    iter, uid, ret);
				failed++;
				continue;
			}

			ret = psa_its_remove(uid);
			if (ret != PSA_SUCCESS) {
				shell_print(sh, "  Iter %ld, UID %u: REMOVE FAILED (%d)",
					    iter, uid, ret);
				failed++;
				continue;
			}
			passed++;
		}
	}

	shell_print(sh, "Stress test complete: %d passed, %d failed",
		    passed, failed);
	return failed ? -EIO : 0;
}

/* ================================================================
 * its info - Display transform configuration
 * ================================================================ */
static int cmd_its_info(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "=== ITS Transform Info ===");

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_MODULE
	shell_print(sh, "Transform Module:     ENABLED");
#else
	shell_print(sh, "Transform Module:     DISABLED");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_IMPLEMENTATION_AEAD
	shell_print(sh, "Implementation:       AEAD");
#else
	shell_print(sh, "Implementation:       NONE");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_SCHEME_AES_GCM
	shell_print(sh, "AEAD Scheme:          AES-GCM");
#else
	shell_print(sh, "AEAD Scheme:          NONE");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_KEY_PROVIDER_DEVICE_ID_HASH
	shell_print(sh, "Key Provider:         DEVICE_ID_HASH");
#else
	shell_print(sh, "Key Provider:         NONE");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_NONCE_PROVIDER_DEFAULT
	shell_print(sh, "Nonce Provider:       DEFAULT");
#else
	shell_print(sh, "Nonce Provider:       NONE");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_KEY_SIZE
	shell_print(sh, "Key Size:             %d bytes",
		    CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_KEY_SIZE);
#else
	shell_print(sh, "Key Size:             N/A");
#endif

#ifdef CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_NONCE_SIZE
	shell_print(sh, "Nonce Size:           %d bytes",
		    CONFIG_SECURE_STORAGE_ITS_TRANSFORM_AEAD_NONCE_SIZE);
#else
	shell_print(sh, "Nonce Size:           N/A");
#endif

	shell_print(sh, "==========================");
	return 0;
}

/* ================================================================
 * its selftest - Verify set/get/remove roundtrip
 * ================================================================ */
static int cmd_its_selftest(const struct shell *sh, size_t argc, char **argv)
{
	psa_storage_uid_t uid = 0x1F;
	uint8_t data[] = "ITS_SELFTEST_DATA_1234";
	uint8_t buf[64];
	size_t len;
	psa_status_t ret;
	int passed = 0, failed = 0;

	shell_print(sh, "=== ITS Selftest ===");

	/* 1. Remove any existing data at UID */
	ret = psa_its_remove(uid);
	if (ret != PSA_SUCCESS && ret != PSA_ERROR_DOES_NOT_EXIST) {
		shell_print(sh, "  [FAIL] Initial remove: %d", ret);
		failed++;
	} else {
		shell_print(sh, "  [PASS] Initial remove (or not exist)");
		passed++;
	}

	/* 2. Set data */
	ret = psa_its_set(uid, sizeof(data), data, 0);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "  [FAIL] psa_its_set: %d", ret);
		failed++;
	} else {
		shell_print(sh, "  [PASS] psa_its_set (%zu bytes)", sizeof(data));
		passed++;
	}

	/* 3. Get data and verify */
	ret = psa_its_get(uid, 0, sizeof(buf), buf, &len);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "  [FAIL] psa_its_get: %d", ret);
		failed++;
	} else if (len != sizeof(data) || memcmp(buf, data, len) != 0) {
		shell_print(sh, "  [FAIL] Data mismatch (len=%zu, expected=%zu)",
			    len, sizeof(data));
		failed++;
	} else {
		shell_print(sh, "  [PASS] psa_its_get roundtrip OK");
		passed++;
	}

	/* 4. Get info */
	struct psa_storage_info_t info;
	ret = psa_its_get_info(uid, &info);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "  [FAIL] psa_its_get_info: %d", ret);
		failed++;
	} else {
		shell_print(sh, "  [PASS] psa_its_get_info: size=%zu, flags=0x%x",
			    info.size, info.flags);
		passed++;
	}

	/* 5. Remove */
	ret = psa_its_remove(uid);
	if (ret != PSA_SUCCESS) {
		shell_print(sh, "  [FAIL] psa_its_remove: %d", ret);
		failed++;
	} else {
		shell_print(sh, "  [PASS] psa_its_remove OK");
		passed++;
	}

	/* 6. Verify removal (get should fail) */
	ret = psa_its_get(uid, 0, sizeof(buf), buf, &len);
	if (ret == PSA_SUCCESS) {
		shell_print(sh, "  [FAIL] Data still exists after remove");
		failed++;
	} else {
		shell_print(sh, "  [PASS] Data correctly removed (get=%d)", ret);
		passed++;
	}

	shell_print(sh, "=========================");
	shell_print(sh, "Selftest: %d passed, %d failed", passed, failed);
	return failed ? -EIO : 0;
}

/* ================================================================
 * Shell command registration
 * ================================================================ */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_its,
	SHELL_CMD_ARG(set, NULL,
		      "Set data: its set <uid> <hex_data>",
		      cmd_its_set, 3, 0),
	SHELL_CMD_ARG(get, NULL,
		      "Get data: its get <uid>",
		      cmd_its_get, 2, 0),
	SHELL_CMD_ARG(get_info, NULL,
		      "Get metadata: its get_info <uid>",
		      cmd_its_get_info, 2, 0),
	SHELL_CMD_ARG(remove, NULL,
		      "Remove data: its remove <uid>",
		      cmd_its_remove, 2, 0),
	SHELL_CMD_ARG(set_once, NULL,
		      "Set write-once: its set_once <uid> <hex_data>",
		      cmd_its_set_once, 3, 0),
	SHELL_CMD_ARG(stress, NULL,
		      "Stress test: its stress [count]",
		      cmd_its_stress, 1, 1),
	SHELL_CMD_ARG(info, NULL,
		      "Show ITS transform configuration",
		      cmd_its_info, 1, 0),
	SHELL_CMD_ARG(selftest, NULL,
		      "Run ITS selftest (set/get/remove roundtrip)",
		      cmd_its_selftest, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(its, &sub_its, "PSA ITS commands", NULL);
