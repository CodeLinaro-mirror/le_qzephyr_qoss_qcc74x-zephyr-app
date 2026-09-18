/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * PSA Hash Shell Commands
 *
 * Provides interactive shell commands for SHA hash computation via PSA Crypto API.
 * Uses psa_hash_compute() / psa_hash_setup/update/finish() / psa_hash_clone() so
 * both HW transparent driver and SW fallback paths are exercised transparently.
 *
 * Commands:
 *   hash list
 *   hash help
 *   hash oneshot       <alg> <hex_data>
 *   hash multipart     <alg> <hex_chunk1> [hex_chunk2 ... hex_chunk10]
 *   hash clone         <alg> <common_hex> <suffix1_hex> <suffix2_hex>
 *   hash str-oneshot   <alg> <string>
 *   hash str-multipart <alg> <string1> [string2 ... string10]
 *   hash str-clone     <alg> <common_str> <suffix1_str> <suffix2_str>
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <psa/crypto.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ========================================================================
 * Algorithm name table
 * ======================================================================== */

struct alg_entry {
	const char      *name;
	psa_algorithm_t  alg;
	size_t           digest_len;
};

static const struct alg_entry alg_table[] = {
	{ "sha1",       PSA_ALG_SHA_1,       20 },
	{ "sha224",     PSA_ALG_SHA_224,     28 },
	{ "sha256",     PSA_ALG_SHA_256,     32 },
	{ "sha384",     PSA_ALG_SHA_384,     48 },
	{ "sha512",     PSA_ALG_SHA_512,     64 },
	{ "sha512_224", PSA_ALG_SHA_512_224, 28 },
	{ "sha512_256", PSA_ALG_SHA_512_256, 32 },
};

#define ALG_TABLE_SIZE ARRAY_SIZE(alg_table)

static const struct alg_entry *find_alg(const char *name)
{
	for (size_t i = 0; i < ALG_TABLE_SIZE; i++) {
		if (strcmp(alg_table[i].name, name) == 0) {
			return &alg_table[i];
		}
	}
	return NULL;
}

/* ========================================================================
 * Hex string helpers
 * ======================================================================== */

/* Parse one hex nibble; returns -1 on invalid character */
static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/*
 * Decode hex string into buf[].
 * Returns number of bytes written, or -1 on error (odd length / invalid char).
 */
static int hex_decode(const char *hex, uint8_t *buf, size_t buf_len)
{
	size_t hex_len = strlen(hex);

	if (hex_len % 2 != 0) {
		return -1;
	}
	if (hex_len / 2 > buf_len) {
		return -1;
	}
	for (size_t i = 0; i < hex_len; i += 2) {
		int hi = hex_nibble(hex[i]);
		int lo = hex_nibble(hex[i + 1]);

		if (hi < 0 || lo < 0) {
			return -1;
		}
		buf[i / 2] = (uint8_t)((hi << 4) | lo);
	}
	return (int)(hex_len / 2);
}

/* ========================================================================
 * Print digest as hex
 * ======================================================================== */

static void print_digest(const struct shell *sh,
			  const char *alg_name,
			  const char *label,
			  const uint8_t *digest,
			  size_t digest_len)
{
	shell_fprintf(sh, SHELL_NORMAL, "[hash] alg=%s %s=", alg_name, label);
	for (size_t i = 0; i < digest_len; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x", digest[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");
}

/* ========================================================================
 * Scratch buffers — shared, shell is single-threaded
 * ======================================================================== */

#define HASH_SHELL_MAX_INPUT 1024

static uint8_t g_input_buf[HASH_SHELL_MAX_INPUT];
static uint8_t g_digest1_buf[PSA_HASH_MAX_SIZE];
static uint8_t g_digest2_buf[PSA_HASH_MAX_SIZE];


/* ========================================================================
 * hash list
 * ======================================================================== */

static int cmd_hash_list(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Supported algorithms:");
	for (size_t i = 0; i < ALG_TABLE_SIZE; i++) {
		shell_print(sh, "  %-12s  digest=%u bytes",
			    alg_table[i].name,
			    (unsigned)alg_table[i].digest_len);
	}
	shell_print(sh, "");
	shell_print(sh, "Available commands:");
	shell_print(sh, "  hash oneshot       <alg> <hex_data>");
	shell_print(sh, "  hash multipart     <alg> <hex_chunk1> [hex_chunk2 ...] (max 10 parts)");
	shell_print(sh, "  hash clone         <alg> <common_hex> <suffix1_hex> <suffix2_hex>");
	shell_print(sh, "  hash str_oneshot   <alg> <string>");
	shell_print(sh, "  hash str_multipart <alg> <string1> [string2 ...]       (max 10 parts)");
	shell_print(sh, "  hash str_clone     <alg> <common_str> <suffix1_str> <suffix2_str>");
	shell_print(sh, "Run 'hash help' for detailed usage and cross-verification examples.");
	return 0;
}

/* ========================================================================
 * hash help
 * ======================================================================== */

static int cmd_hash_help(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh,
"PSA Hash Shell Commands  \n"
"=======================  \n"
"\n"
"  hash list  \n"
"      List supported algorithms and available commands.  \n"
"\n"
"  hash oneshot <alg> <hex_data>  \n"
"      One-shot hash of hex-encoded input via psa_hash_compute(). \n"
"      <alg>      : sha1 | sha224 | sha256 | sha384 | sha512 | sha512_224 | sha512_256 \n"
"      <hex_data> : even-length hex string, e.g. 68656c6c6f (= \"hello\") \n"
"      Example: \n"
"        hash oneshot sha256 '' \n"
"        (MINGW64 cmd) printf ''| openssl dgst -sha256 \n"
"        hash oneshot sha256 68656c6c6f \n"
"        (MINGW64 cmd) printf '\\x68\\x65\\x6c\\x6c\\x6f'| openssl dgst -sha256 \n"
"\n"
"\n"
"  hash multipart <alg> <hex_chunk1> [hex_chunk2 ...]  (max 10 parts) \n"
"      Multi-part hash via psa_hash_setup/update/finish(). \n"
"      Each argument is a separate hex-encoded chunk fed by one update call. \n"
"      Example (two chunks spelling \"hello\"): \n"
"        hash multipart sha256 68656c 6c6f \n"
"        (MINGW64 cmd) printf '\\x68\\x65\\x6c\\x6c\\x6f'| openssl dgst -sha256 \n"
"\n"
"\n"
"  hash clone <alg> <common_hex> <suffix1_hex> <suffix2_hex> \n"
"      Clone test via psa_hash_clone(). \n"
"      Flow: setup -> update(common) -> clone -> [op1: update(suffix1)+finish] \n"
"                                               [op2: update(suffix2)+finish] \n"
"      Prints digest1 = hash(common+suffix1) and digest2 = hash(common+suffix2). \n"
"      Example: common=\"hel\", suffix1=\"lo\", suffix2=\"p\" \n"
"        hash clone sha256 68656c 6c6f 70 \n"
"      digest1 should equal: hash oneshot sha256 68656c6c6f   (\"hello\") \n"
"      digest2 should equal: hash oneshot sha256 68656c70     (\"help\") \n"
"        (MINGW64 cmd) printf '\\x68\\x65\\x6c\\x6c\\x6f'| openssl dgst -sha256 \n"
"        (MINGW64 cmd) printf '\\x68\\x65\\x6c\\x70'     | openssl dgst -sha256 \n"
"\n"
"\n"
"  hash str_oneshot <alg> <string> \n"
"      One-shot hash of a plain ASCII string via psa_hash_compute(). \n"
"      Example: \n"
"        hash str_oneshot sha256 \"\" \n"
"        (MINGW64 cmd) printf \"\" | openssl dgst -sha256 \n"
"        hash str_oneshot sha256 hello \n"
"        (MINGW64 cmd) printf \"hello\" | openssl dgst -sha256 \n"
"\n"
"\n"
"  hash str_multipart <alg> <string1> [string2 ...]  (max 10 parts) \n"
"      Multi-part hash where each argument is a plain ASCII string chunk. \n"
"      Example (two chunks): \n"
"        hash str_multipart sha256 hel lo \n"
"        (MINGW64 cmd) printf \"hello\" | openssl dgst -sha256 \n"
"\n"
"\n"
"  hash str_clone <alg> <common_str> <suffix1_str> <suffix2_str> \n"
"      Clone test with plain ASCII string input. \n"
"      Example: common=\"hel\", suffix1=\"lo\", suffix2=\"p\" \n"
"        hash str_clone sha256 hel lo p \n"
"      digest1 should equal: hash str_oneshot sha256 hello \n"
"      digest2 should equal: hash str_oneshot sha256 help \n"
"        (MINGW64 cmd) printf \"hello\" | openssl dgst -sha256 \n"
"        (MINGW64 cmd) printf \"help\"  | openssl dgst -sha256 \n"
"\n"
"\n"
"Cross-verification in MINGW64/Git Bash (not PowerShell): \n"
"Note: \"hello\" in hex is 68656c6c6f \n"
"\n"
"┌─────────────────────────────────────────┬──────────────────────────────────────────────┬──────────────────────────────────────────────────────────────────┐ \n"
"│ Hash Shell Commands                     │ MINGW64 Commands                             │ Expected Digests                                                 │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash oneshot sha1 \"\"             │ printf \"\" | openssl dgst -sha1             │ da39a3ee5e6b4b0d3255bfef95601890afd80709                         │ \n"
"│ qcom hash oneshot sha224 \"\"           │ printf \"\" | openssl dgst -sha224           │ d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f         │ \n"
"│ qcom hash oneshot sha256 \"\"           │ printf \"\" | openssl dgst -sha256           │ e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 │ \n"
"│ qcom hash oneshot sha384 \"\"           │ printf \"\" | openssl dgst -sha384           │ 38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da │ \n"
"│                                         │                                              │ 274edebfe76f65fbd51ad2f14898b95b                                 │ \n"
"│ qcom hash oneshot sha512 \"\"           │ printf \"\" | openssl dgst -sha512           │ cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce │ \n"
"│                                         │                                              │ 47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e │ \n"
"│ qcom hash oneshot sha512_224 \"\"       │ printf \"\" | openssl dgst -sha512-224       │ 6ed0dd02806fa89e25de060c19d3ac86cabb87d6a0ddd05c333b84f4         │ \n"
"│ qcom hash oneshot sha512_256 \"\"       │ printf \"\" | openssl dgst -sha512-256       │ c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a │ \n"
"│ qcom hash str_oneshot sha256 \"\"       │ printf \"\" | openssl dgst -sha256           │ e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash str_oneshot sha1 hello        │ printf \"hello\" | openssl dgst -sha1        │ aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d                         │ \n"
"│ qcom hash str_oneshot sha224 hello      │ printf \"hello\" | openssl dgst -sha224      │ ea09ae9cc6768c50fcee903ed054556e5bfc8347907f12598aa24193         │ \n"
"│ qcom hash str_oneshot sha256 hello      │ printf \"hello\" | openssl dgst -sha256      │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│ qcom hash str_oneshot sha384 hello      │ printf \"hello\" | openssl dgst -sha384      │ 59e1748777448c69de6b800d7a33bbfb9ff1b463e44354c3553bcdb9c666fa90 │ \n"
"│                                         │                                              │ 125a3c79f90397bdf5f6a13de828684f                                 │ \n"
"│ qcom hash str_oneshot sha512 hello      │ printf \"hello\" | openssl dgst -sha512      │ 9b71d224bd62f3785d96d46ad3ea3d73319bfbc2890caadae2dff72519673ca7 │ \n"
"│                                         │                                              │ 2323c3d99ba5c11d7c7acc6e14b8c5da0c4663475c2e5c3adef46f73bcdec043 │ \n"
"│ qcom hash str_oneshot sha512_224 hello  │ printf \"hello\" | openssl dgst -sha512-224  │ fe8509ed1fb7dcefc27e6ac1a80eddbec4cb3d2c6fe565244374061c         │ \n"
"│ qcom hash str_oneshot sha512_256 hello  │ printf \"hello\" | openssl dgst -sha512-256  │ e30d87cfa2a75db545eac4d61baf970366a8357c7f72fa95b52d0accb698f13a │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash clone sha256 aabbcc 1122 3344 │ printf '\\xAA\\xBB\\xCC\\x11\\x22'           │ db096c2dcfdcab295426a7ebcf33dd45c03c208e9c4fbff0bf337c0bc450d22c │ \n"
"│                                         │   | openssl dgst -sha256                     │ (clone digest1: common=aabbcc suffix1=1122)                      │ \n"
"│                                         │ printf '\\xAA\\xBB\\xCC\\x33\\x44'           │ e7e24abb9cc36a056e5023fd69df401d46539dd4db272fb4f70b029540641d1e │ \n"
"│                                         │   | openssl dgst -sha256                     │ (clone digest2: common=aabbcc suffix2=3344)                      │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash oneshot sha256 68656c6c6f     │ printf '\\x68\\x65\\x6c\\x6c\\x6f'           │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│                                         │   | openssl dgst -sha256                     │ (= SHA-256(\"hello\"))                                           │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash multipart sha256 68656c 6c6f  │ printf '\\x68\\x65\\x6c\\x6c\\x6f'           │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│                                         │   | openssl dgst -sha256                     │ (= SHA-256(\"hello\"), same as oneshot above)                    │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash clone sha256 68656c 6c6f 70   │ printf '\\x68\\x65\\x6c\\x6c\\x6f'           │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│        (digest1 = \"hel\"+\"lo\")       │   | openssl dgst -sha256                     │ (= SHA-256(\"hello\"))                                           │ \n"
"│        (digest2 = \"hel\"+\"p\")        │ printf '\\x68\\x65\\x6c\\x70'                │ 106a5842fc5fce6f663176285ed1516dbb1e3d15c05abab12fdca46d60b539b7 │ \n"
"│                                         │   | openssl dgst -sha256                     │                                                                  │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash str_multipart sha256 hel lo   │ printf \"hello\" | openssl dgst -sha256      │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│                                         │                                              │ (= SHA-256(\"hello\"), same as str_oneshot above)                │ \n"
"├─────────────────────────────────────────┼──────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤ \n"
"│ qcom hash str_clone sha256 hel lo p     │ printf \"hello\" | openssl dgst -sha256      │ 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 │ \n"
"│        (digest1 = \"hel\"+\"lo\")       │                                              │ (= SHA-256(\"hello\"))                                           │ \n"
"│        (digest2 = \"hel\"+\"p\")        │ printf \"help\"  | openssl dgst -sha256      │ 106a5842fc5fce6f663176285ed1516dbb1e3d15c05abab12fdca46d60b539b7 │ \n"
"└─────────────────────────────────────────┴──────────────────────────────────────────────┴──────────────────────────────────────────────────────────────────┘ \n"
	);
	return 0;
}

/* ========================================================================
 * hash oneshot  (hex input)
 * ======================================================================== */

static int cmd_hash_oneshot(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: hash oneshot <alg> <hex_data>");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	int input_len = hex_decode(argv[2], g_input_buf, sizeof(g_input_buf));

	if (input_len < 0) {
		shell_error(sh, "Invalid hex string (odd length or invalid chars): %s", argv[2]);
		return -EINVAL;
	}

	size_t digest_len = 0;
	psa_status_t status = psa_hash_compute(entry->alg,
					       g_input_buf, (size_t)input_len,
					       g_digest1_buf, sizeof(g_digest1_buf),
					       &digest_len);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_compute failed: %d", (int)status);
		return -EIO;
	}

	print_digest(sh, entry->name, "digest", g_digest1_buf, digest_len);
	return 0;
}

/* ========================================================================
 * hash multipart  (hex input)
 * ======================================================================== */

static int cmd_hash_multipart(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: hash multipart <alg> <hex_chunk1> [hex_chunk2 ...]");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_status_t status = psa_hash_setup(&op, entry->alg);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_setup failed: %d", (int)status);
		return -EIO;
	}

	for (size_t i = 2; i < argc; i++) {
		int chunk_len = hex_decode(argv[i], g_input_buf, sizeof(g_input_buf));

		if (chunk_len < 0) {
			shell_error(sh, "Invalid hex chunk[%u]: %s", (unsigned)(i - 2), argv[i]);
			psa_hash_abort(&op);
			return -EINVAL;
		}

		status = psa_hash_update(&op, g_input_buf, (size_t)chunk_len);
		if (status != PSA_SUCCESS) {
			shell_error(sh, "psa_hash_update[%u] failed: %d",
				    (unsigned)(i - 2), (int)status);
			psa_hash_abort(&op);
			return -EIO;
		}
	}

	size_t digest_len = 0;

	status = psa_hash_finish(&op, g_digest1_buf, sizeof(g_digest1_buf), &digest_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish failed: %d", (int)status);
		psa_hash_abort(&op);
		return -EIO;
	}

	print_digest(sh, entry->name, "digest", g_digest1_buf, digest_len);
	return 0;
}

/* ========================================================================
 * hash clone  (hex input)
 * ======================================================================== */

static int cmd_hash_clone(const struct shell *sh, size_t argc, char **argv)
{
	psa_hash_operation_t op1 = PSA_HASH_OPERATION_INIT;
	psa_hash_operation_t op2 = PSA_HASH_OPERATION_INIT;

	if (argc != 5) {
		shell_error(sh,
			"Usage: hash clone <alg> <common_hex> <suffix1_hex> <suffix2_hex>");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	/* Decode common prefix */
	int common_len = hex_decode(argv[2], g_input_buf, sizeof(g_input_buf));

	if (common_len < 0) {
		shell_error(sh, "Invalid hex for <common_hex>: %s", argv[2]);
		return -EINVAL;
	}

	/* setup + update(common) */
	psa_status_t status = psa_hash_setup(&op1, entry->alg);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_setup failed: %d", (int)status);
		return -EIO;
	}

	status = psa_hash_update(&op1, g_input_buf, (size_t)common_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(common) failed: %d", (int)status);
		psa_hash_abort(&op1);
		return -EIO;
	}


	status = psa_hash_clone(&op1, &op2);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_clone failed: %d", (int)status);
		psa_hash_abort(&op1);
		return -EIO;
	}

	/* op1: update(suffix1) + finish → digest1 */
	int s1_len = hex_decode(argv[3], g_input_buf, sizeof(g_input_buf));

	if (s1_len < 0) {
		shell_error(sh, "Invalid hex for <suffix1_hex>: %s", argv[3]);
		psa_hash_abort(&op1);
		psa_hash_abort(&op2);
		return -EINVAL;
	}

	status = psa_hash_update(&op1, g_input_buf, (size_t)s1_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(suffix1) failed: %d", (int)status);
		psa_hash_abort(&op1);
		psa_hash_abort(&op2);
		return -EIO;
	}

	size_t digest1_len = 0;

	status = psa_hash_finish(&op1, g_digest1_buf, sizeof(g_digest1_buf), &digest1_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish(op1) failed: %d", (int)status);
		psa_hash_abort(&op1);
		psa_hash_abort(&op2);
		return -EIO;
	}

	/* op2: update(suffix2) + finish → digest2 */
	int s2_len = hex_decode(argv[4], g_input_buf, sizeof(g_input_buf));

	if (s2_len < 0) {
		shell_error(sh, "Invalid hex for <suffix2_hex>: %s", argv[4]);
		psa_hash_abort(&op2);
		return -EINVAL;
	}

	status = psa_hash_update(&op2, g_input_buf, (size_t)s2_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(suffix2) failed: %d", (int)status);
		psa_hash_abort(&op2);
		return -EIO;
	}

	size_t digest2_len = 0;

	status = psa_hash_finish(&op2, g_digest2_buf, sizeof(g_digest2_buf), &digest2_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish(op2) failed: %d", (int)status);
		psa_hash_abort(&op2);
		return -EIO;
	}

	/* Print both results */
	print_digest(sh, entry->name, "digest1(common+suffix1)", g_digest1_buf, digest1_len);
	print_digest(sh, entry->name, "digest2(common+suffix2)", g_digest2_buf, digest2_len);
	return 0;
}

/* ========================================================================
 * hash str-oneshot  (ASCII string input)
 * ======================================================================== */

static int cmd_hash_str_oneshot(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: hash str_oneshot <alg> <string>");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	size_t digest_len = 0;
	psa_status_t status = psa_hash_compute(entry->alg,
					       (const uint8_t *)argv[2], strlen(argv[2]),
					       g_digest1_buf, sizeof(g_digest1_buf),
					       &digest_len);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_compute failed: %d", (int)status);
		return -EIO;
	}

	print_digest(sh, entry->name, "digest", g_digest1_buf, digest_len);
	return 0;
}

/* ========================================================================
 * hash str-multipart  (ASCII string input)
 * ======================================================================== */

static int cmd_hash_str_multipart(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: hash str_multipart <alg> <string1> [string2 ...]");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_status_t status = psa_hash_setup(&op, entry->alg);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_setup failed: %d", (int)status);
		return -EIO;
	}

	for (size_t i = 2; i < argc; i++) {
		status = psa_hash_update(&op,
					 (const uint8_t *)argv[i],
					 strlen(argv[i]));
		if (status != PSA_SUCCESS) {
			shell_error(sh, "psa_hash_update[%u] failed: %d",
				    (unsigned)(i - 2), (int)status);
			psa_hash_abort(&op);
			return -EIO;
		}
	}

	size_t digest_len = 0;

	status = psa_hash_finish(&op, g_digest1_buf, sizeof(g_digest1_buf), &digest_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish failed: %d", (int)status);
		psa_hash_abort(&op);
		return -EIO;
	}

	print_digest(sh, entry->name, "digest", g_digest1_buf, digest_len);
	return 0;
}

/* ========================================================================
 * hash str-clone  (ASCII string input)
 * ======================================================================== */

static int cmd_hash_str_clone(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 5) {
		shell_error(sh,
			"Usage: hash str_clone <alg> <common_str> <suffix1_str> <suffix2_str>");
		return -EINVAL;
	}

	const struct alg_entry *entry = find_alg(argv[1]);

	if (!entry) {
		shell_error(sh, "Unknown algorithm '%s'. Run 'hash list'.", argv[1]);
		return -EINVAL;
	}

	/* setup + update(common) */
	psa_hash_operation_t op1 = PSA_HASH_OPERATION_INIT;
	psa_status_t status = psa_hash_setup(&op1, entry->alg);

	if (status == PSA_ERROR_NOT_SUPPORTED) {
		shell_warn(sh, "[hash] alg=%s NOT SUPPORTED (no HW driver or SW fallback)",
			   entry->name);
		return 0;
	}
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_setup failed: %d", (int)status);
		return -EIO;
	}

	status = psa_hash_update(&op1,
				 (const uint8_t *)argv[2], strlen(argv[2]));
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(common) failed: %d", (int)status);
		psa_hash_abort(&op1);
		return -EIO;
	}

	/* clone op1 → op2 */
	psa_hash_operation_t op2 = PSA_HASH_OPERATION_INIT;

	status = psa_hash_clone(&op1, &op2);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_clone failed: %d", (int)status);
		psa_hash_abort(&op1);
		return -EIO;
	}

	/* op1: update(suffix1) + finish → digest1 */
	status = psa_hash_update(&op1,
				 (const uint8_t *)argv[3], strlen(argv[3]));
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(suffix1) failed: %d", (int)status);
		psa_hash_abort(&op1);
		psa_hash_abort(&op2);
		return -EIO;
	}

	size_t digest1_len = 0;

	status = psa_hash_finish(&op1, g_digest1_buf, sizeof(g_digest1_buf), &digest1_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish(op1) failed: %d", (int)status);
		psa_hash_abort(&op1);
		psa_hash_abort(&op2);
		return -EIO;
	}

	/* op2: update(suffix2) + finish → digest2 */
	status = psa_hash_update(&op2,
				 (const uint8_t *)argv[4], strlen(argv[4]));
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_update(suffix2) failed: %d", (int)status);
		psa_hash_abort(&op2);
		return -EIO;
	}

	size_t digest2_len = 0;

	status = psa_hash_finish(&op2, g_digest2_buf, sizeof(g_digest2_buf), &digest2_len);
	if (status != PSA_SUCCESS) {
		shell_error(sh, "psa_hash_finish(op2) failed: %d", (int)status);
		psa_hash_abort(&op2);
		return -EIO;
	}

	/* Print both results */
	print_digest(sh, entry->name, "digest1(common+suffix1)", g_digest1_buf, digest1_len);
	print_digest(sh, entry->name, "digest2(common+suffix2)", g_digest2_buf, digest2_len);
	return 0;
}

/* ========================================================================
 * Shell command registration
 * ======================================================================== */

SHELL_STATIC_SUBCMD_SET_CREATE(hash_commands,
	SHELL_CMD(list,              NULL,  "List supported algorithms and commands",                                                  cmd_hash_list),
	SHELL_CMD(help,              NULL,  "Show detailed help and cross-verification examples",                                      cmd_hash_help),
	SHELL_CMD_ARG(oneshot,       NULL,  "One-shot hash (hex input): hash oneshot <alg> <hex_data>",                                cmd_hash_oneshot,        3, 0),
	SHELL_CMD_ARG(multipart,     NULL,  "Multi-part hash (hex input): hash multipart <alg> <hex_chunk1> [...] (max 10)",           cmd_hash_multipart,      3, 9),
	SHELL_CMD_ARG(clone,         NULL,  "Clone test (hex input): hash clone <alg> <common_hex> <suffix1_hex> <suffix2_hex>",       cmd_hash_clone,          5, 0),
	SHELL_CMD_ARG(str_oneshot,   NULL,  "One-shot hash (ASCII input): hash str_oneshot <alg> <string>",                            cmd_hash_str_oneshot,    3, 0),
	SHELL_CMD_ARG(str_multipart, NULL,  "Multi-part hash (ASCII input): hash str_multipart <alg> <string1> [...] (max 10)",        cmd_hash_str_multipart,  3, 9),
	SHELL_CMD_ARG(str_clone,     NULL,  "Clone test (ASCII input): hash str_clone <alg> <common_str> <suffix1_str> <suffix2_str>", cmd_hash_str_clone,      5, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((qcom), hash, &hash_commands, "hash commands", NULL, 1, 0);

