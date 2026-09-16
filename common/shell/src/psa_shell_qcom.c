/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * QCC74x PSA Crypto Shell Commands
 *
 * Interactive AES encryption/decryption, selftest, and benchmark
 * via the Zephyr Shell framework.
 *
 * Command tree:
 *   aes
 *     selftest              — Run NIST KAT vectors for all AES modes
 *     info                  — Display HW/SW AES capability info
 *     encrypt <mode> <key_bits> <hex_key> [hex_iv] <hex_plain>
 *     decrypt <mode> <key_bits> <hex_key> [hex_iv] <hex_cipher>
 *     bench   <mode> <key_bits> <payload_kb>
 *
 *   Modes: ecb | cbc | ctr
 *   Key sizes: 128 | 192 | 256
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <psa/crypto.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

/** Convert a hex character to nibble (0..15). Returns -1 on error. */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/**
 * @brief Convert a hex string to a byte array.
 *
 * @param hex     Null-terminated hex string (no "0x" prefix).
 * @param out     Output buffer.
 * @param out_sz  Output buffer size.
 * @return Number of bytes written, or -1 on error.
 */
static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_sz)
{
    size_t len = strlen(hex);
    size_t byte_len = len / 2;

    if ((len % 2) != 0 || byte_len > out_sz) {
        return -1;
    }

    for (size_t i = 0; i < byte_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return (int)byte_len;
}

/** Map a mode string to a PSA algorithm. */
static psa_algorithm_t mode_to_alg(const char *mode)
{
    if (strcmp(mode, "ecb") == 0) return PSA_ALG_ECB_NO_PADDING;
    if (strcmp(mode, "cbc") == 0) return PSA_ALG_CBC_NO_PADDING;
    if (strcmp(mode, "ctr") == 0) return PSA_ALG_CTR;
    return PSA_ALG_NONE;
}

/** Determine if an algorithm is a block-cipher mode requiring aligned input. */
static bool alg_needs_aligned_input(psa_algorithm_t alg)
{
    return (alg == PSA_ALG_ECB_NO_PADDING || alg == PSA_ALG_CBC_NO_PADDING);
}

/** Check whether an algorithm uses an IV. */
static bool alg_needs_iv(psa_algorithm_t alg)
{
    return (alg != PSA_ALG_ECB_NO_PADDING);
}

/**
 * @brief Import an AES key into the PSA key store.
 *
 * @param key_buf  Raw key bytes.
 * @param key_len  Key length in bytes.
 * @param alg      PSA algorithm.
 * @param usage    PSA key usage flags.
 * @param key_id   [out] Imported key ID.
 * @return PSA_SUCCESS on success.
 */
static psa_status_t import_aes_key(const uint8_t *key_buf, size_t key_len,
                                   psa_algorithm_t alg,
                                   psa_key_usage_t usage,
                                   psa_key_id_t *key_id)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, key_len * 8);
    psa_set_key_usage_flags(&attr, usage);
    psa_set_key_algorithm(&attr, alg);

    psa_status_t status = psa_import_key(&attr, key_buf, key_len, key_id);
    psa_reset_key_attributes(&attr);
    return status;
}

/* =========================================================================
 * AES supported algorithm / block size info
 * ======================================================================= */

/**
 * @brief Return the AES block size and indicate whether @p alg is supported.
 *
 * @param alg         PSA algorithm to query.
 * @param block_size  [out] AES block size (always 16 for supported modes).
 * @return true if the algorithm is supported, false otherwise.
 */
static bool aes_mode_info(psa_algorithm_t alg, size_t *block_size)
{
    switch (alg) {
    case PSA_ALG_ECB_NO_PADDING:
    case PSA_ALG_CBC_NO_PADDING:
    case PSA_ALG_CTR:
        *block_size = 16;
        return true;
    default:
        *block_size = 0;
        return false;
    }
}

/* =========================================================================
 * NIST KAT vectors for AES selftest
 *
 * These are the NIST FIPS-197 Appendix B / NIST SP 800-38A vectors.
 * They are used both by the ztest suite and by the shell 'selftest' command.
 * ======================================================================= */

/* AES-128 key (NIST FIPS-197) */
static const uint8_t kat_aes128_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

/* AES-128 plaintext (NIST FIPS-197) */
static const uint8_t kat_aes128_plain[16] = {
    0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
    0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
};

/* AES-128 expected ECB ciphertext */
static const uint8_t kat_aes128_ecb_ct[16] = {
    0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
    0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32
};

/* AES-128-CBC: key, iv, plaintext block 0, expected ciphertext */
static const uint8_t kat_aes128_cbc_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t kat_aes128_cbc_iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
static const uint8_t kat_aes128_cbc_pt[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
};
static const uint8_t kat_aes128_cbc_ct[16] = {
    0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
    0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d
};

/* AES-128-CTR: same key, iv=0xF0F1..FF, plain, expected */
static const uint8_t kat_aes128_ctr_iv[16] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
static const uint8_t kat_aes128_ctr_pt[64] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
    0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
    0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
    0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};
static const uint8_t kat_aes128_ctr_ct[64] = {
    0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
    0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
    0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
    0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
    0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
    0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
    0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
    0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
};

/**
 * @brief Encrypt with a user-supplied IV using the multipart streaming API.
 *
 * psa_cipher_encrypt() one-shot generates its own random IV, which is not
 * suitable for selftest where we need to verify against known-answer-test
 * vectors that use a specific IV.  This helper uses the streaming API
 * (setup + set_iv + update + finish) so we can supply the exact IV.
 */
static psa_status_t encrypt_with_iv(psa_key_id_t key_id,
                                    psa_algorithm_t alg,
                                    const uint8_t *iv, size_t iv_len,
                                    const uint8_t *input, size_t input_len,
                                    uint8_t *output, size_t output_size,
                                    size_t *output_len)
{
    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    psa_status_t status;

    status = psa_cipher_encrypt_setup(&op, key_id, alg);
    if (status != PSA_SUCCESS) {
        return status;
    }

    if (alg_needs_iv(alg) && iv != NULL) {
        status = psa_cipher_set_iv(&op, iv, iv_len);
        if (status != PSA_SUCCESS) {
            psa_cipher_abort(&op);
            return status;
        }
    }

    size_t out_len = 0;
    status = psa_cipher_update(&op, input, input_len,
                               output, output_size, &out_len);
    if (status != PSA_SUCCESS) {
        psa_cipher_abort(&op);
        return status;
    }

    size_t finish_len = 0;
    status = psa_cipher_finish(&op, output + out_len,
                               output_size - out_len, &finish_len);
    if (status != PSA_SUCCESS) {
        psa_cipher_abort(&op);
        return status;
    }

    *output_len = out_len + finish_len;
    return PSA_SUCCESS;
}

/**
 * @brief Run a single AES KAT test.
 *
 * @param sh        Shell handle for printing.
 * @param label     Test label (e.g. "ECB-128").
 * @param alg       PSA algorithm.
 * @param key       Key bytes.
 * @param key_len   Key length in bytes.
 * @param iv        IV (NULL for ECB).
 * @param iv_len    IV length.
 * @param pt        Plaintext.
 * @param pt_len    Plaintext length.
 * @param expected  Expected ciphertext.
 * @param exp_len   Expected ciphertext length.
 * @return true if test passed.
 */
static bool run_kat(const struct shell *sh, const char *label,
                    psa_algorithm_t alg,
                    const uint8_t *key, size_t key_len,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *pt, size_t pt_len,
                    const uint8_t *expected, size_t exp_len)
{
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    uint8_t output[128];
    size_t out_len;
    psa_status_t status;

    status = import_aes_key(key, key_len, alg, PSA_KEY_USAGE_ENCRYPT, &key_id);
    if (status != PSA_SUCCESS) {
        shell_print(sh, "  %s ... FAIL (key import: %d)", label, status);
        return false;
    }

    /* Use multipart streaming API for all modes so that for CBC/CTR we
     * can supply the exact IV from the test vector.
     * psa_cipher_encrypt() one-shot would generate a random IV internally,
     * which would make the output differ from the expected vector.
     */
    status = encrypt_with_iv(key_id, alg, iv, iv_len,
                             pt, pt_len,
                             output, sizeof(output), &out_len);
    if (status != PSA_SUCCESS) {
        shell_print(sh, "  %s ... FAIL (encrypt: %d)", label, status);
        psa_destroy_key(key_id);
        return false;
    }

    bool pass = (out_len == exp_len && memcmp(output, expected, exp_len) == 0);
    shell_print(sh, "  %s ... %s", label, pass ? "PASS" : "FAIL");
    if (!pass) {
        shell_print(sh, "    expected %zu bytes, got %zu", exp_len, out_len);
    }

    psa_destroy_key(key_id);
    return pass;
}

/* =========================================================================
 * Shell command handlers
 * ======================================================================= */

/** aes selftest — run NIST KAT vectors */
static int cmd_aes_selftest(const struct shell *sh, size_t argc, char **argv)
{
    int passed = 0;
    int failed = 0;

    shell_print(sh, "AES Selftest");

    bool ok;

    /* ECB-128 */
    ok = run_kat(sh, "ECB-128", PSA_ALG_ECB_NO_PADDING,
                 kat_aes128_key, 16, NULL, 0,
                 kat_aes128_plain, 16,
                 kat_aes128_ecb_ct, 16);
    if (ok) passed++; else failed++;

    /* CBC-128 */
    ok = run_kat(sh, "CBC-128", PSA_ALG_CBC_NO_PADDING,
                 kat_aes128_cbc_key, 16,
                 kat_aes128_cbc_iv, 16,
                 kat_aes128_cbc_pt, 16,
                 kat_aes128_cbc_ct, 16);
    if (ok) passed++; else failed++;

    /* CTR-128 */
    ok = run_kat(sh, "CTR-128", PSA_ALG_CTR,
                 kat_aes128_key, 16,
                 kat_aes128_ctr_iv, 16,
                 kat_aes128_ctr_pt, 64,
                 kat_aes128_ctr_ct, 64);
    if (ok) passed++; else failed++;

    shell_print(sh, "Selftest: %d passed, %d failed", passed, failed);
    return (failed == 0) ? 0 : -EIO;
}

/** aes info — display AES capability info */
static int cmd_aes_info(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "QCC74x PSA AES Capabilities:");
    shell_print(sh, "  Supported modes: ecb, cbc, ctr");
    shell_print(sh, "  Key sizes: 128, 192, 256 bits");
    shell_print(sh, "  Block size: 16 bytes");
    shell_print(sh, "  PSA driver: PSA_CRYPTO_DRIVER_QCC74X");
#ifdef PSA_CRYPTO_DRIVER_QCC74X_AES
    shell_print(sh, "  HW acceleration: enabled");
#else
    shell_print(sh, "  HW acceleration: disabled (SW fallback)");
#endif
    shell_print(sh, "  Interface: one-shot + multipart streaming");
    return 0;
}

/** aes encrypt <mode> <key_bits> <hex_key> [hex_iv] <hex_plain> */
static int cmd_aes_encrypt(const struct shell *sh, size_t argc, char **argv)
{
    /* Usage:
     *   ecb: aes encrypt ecb 128 <key> <plain>
     *   cbc: aes encrypt cbc 256 <key> <iv> <plain>
     *   ctr: aes encrypt ctr 128 <key> <iv> <plain>
     */
    if (argc < 5 || argc > 6) {
        shell_error(sh, "Usage: aes encrypt <ecb|cbc|ctr> <key_bits> <hex_key> [hex_iv] <hex_plain>");
        return -EINVAL;
    }

    const char *mode_str = argv[1];
    long key_bits = shell_strtol(argv[2], 10, NULL);
    const char *key_hex = argv[3];
    const char *iv_hex = NULL;
    const char *pt_hex = NULL;

    if (argc == 5) {
        pt_hex = argv[4];
    } else {
        iv_hex = argv[4];
        pt_hex = argv[5];
    }

    psa_algorithm_t alg = mode_to_alg(mode_str);
    if (alg == PSA_ALG_NONE) {
        shell_error(sh, "Unknown mode '%s'. Supported: ecb, cbc, ctr", mode_str);
        return -EINVAL;
    }

    if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
        shell_error(sh, "Invalid key size %ld. Supported: 128, 192, 256", key_bits);
        return -EINVAL;
    }

    if (!alg_needs_iv(alg) && iv_hex != NULL) {
        shell_warn(sh, "ECB mode does not use an IV; ignoring");
        iv_hex = NULL;
    }
    if (alg_needs_iv(alg) && iv_hex == NULL) {
        shell_error(sh, "%s mode requires an IV", mode_str);
        return -EINVAL;
    }

    /* Parse hex arguments */
    uint8_t key_buf[32];
    int key_len = hex_to_bytes(key_hex, key_buf, sizeof(key_buf));
    if (key_len < 0 || (size_t)(key_len * 8) != (size_t)key_bits) {
        shell_error(sh, "Invalid key hex string (expected %ld bits = %ld hex chars)",
                    key_bits, key_bits / 4);
        return -EINVAL;
    }

    uint8_t iv_buf[16];
    int iv_len = 0;
    if (iv_hex != NULL) {
        iv_len = hex_to_bytes(iv_hex, iv_buf, sizeof(iv_buf));
        if (iv_len != 16) {
            shell_error(sh, "Invalid IV hex string (expected 32 hex chars)");
            return -EINVAL;
        }
    }

    uint8_t pt_buf[1024];
    int pt_len = hex_to_bytes(pt_hex, pt_buf, sizeof(pt_buf));
    if (pt_len <= 0) {
        shell_error(sh, "Invalid plaintext hex string");
        return -EINVAL;
    }

    if (alg_needs_aligned_input(alg) && (pt_len % 16) != 0) {
        shell_error(sh, "%s mode requires input length to be multiple of 16 bytes "
                        "(got %d bytes)", mode_str, pt_len);
        return -EINVAL;
    }

    /* Import key */
    psa_key_id_t key_id;
    psa_status_t status = import_aes_key(key_buf, (size_t)key_len, alg,
                                          PSA_KEY_USAGE_ENCRYPT, &key_id);
    if (status != PSA_SUCCESS) {
        shell_error(sh, "Key import failed: %d", status);
        return -EIO;
    }

    /* Encrypt using multipart API so we can inject user IV */
    size_t outbuf_sz = (size_t)pt_len + 16;
    uint8_t *output = (uint8_t *)k_malloc(outbuf_sz);
    if (!output) {
        shell_error(sh, "Failed to allocate output buffer");
        psa_destroy_key(key_id);
        return -ENOMEM;
    }
    size_t out_len;

    status = encrypt_with_iv(key_id, alg,
                             (iv_len > 0) ? iv_buf : NULL, (size_t)iv_len,
                             pt_buf, (size_t)pt_len,
                             output, outbuf_sz, &out_len);
    if (status != PSA_SUCCESS) {
        shell_error(sh, "Encrypt failed: %d", status);
        psa_destroy_key(key_id);
        k_free(output);
        return -EIO;
    }

    psa_destroy_key(key_id);

    shell_print(sh, "Ciphertext (%zu bytes):", out_len);
    shell_hexdump(sh, output, out_len);

    k_free(output);
    return 0;
}

/** aes decrypt <mode> <key_bits> <hex_key> [hex_iv] <hex_cipher> */
static int cmd_aes_decrypt(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 5 || argc > 6) {
        shell_error(sh, "Usage: aes decrypt <ecb|cbc|ctr> <key_bits> <hex_key> [hex_iv] <hex_cipher>");
        return -EINVAL;
    }

    const char *mode_str = argv[1];
    long key_bits = shell_strtol(argv[2], 10, NULL);
    const char *key_hex = argv[3];
    const char *iv_hex = NULL;
    const char *ct_hex = NULL;

    if (argc == 5) {
        ct_hex = argv[4];
    } else {
        iv_hex = argv[4];
        ct_hex = argv[5];
    }

    psa_algorithm_t alg = mode_to_alg(mode_str);
    if (alg == PSA_ALG_NONE) {
        shell_error(sh, "Unknown mode '%s'. Supported: ecb, cbc, ctr", mode_str);
        return -EINVAL;
    }

    if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
        shell_error(sh, "Invalid key size %ld. Supported: 128, 192, 256", key_bits);
        return -EINVAL;
    }

    if (!alg_needs_iv(alg) && iv_hex != NULL) {
        shell_warn(sh, "ECB mode does not use an IV; ignoring");
        iv_hex = NULL;
    }
    if (alg_needs_iv(alg) && iv_hex == NULL) {
        shell_error(sh, "%s mode requires an IV", mode_str);
        return -EINVAL;
    }

    /* Parse hex arguments */
    uint8_t key_buf[32];
    int key_len = hex_to_bytes(key_hex, key_buf, sizeof(key_buf));
    if (key_len < 0 || (size_t)(key_len * 8) != (size_t)key_bits) {
        shell_error(sh, "Invalid key hex string (expected %ld bits)", key_bits);
        return -EINVAL;
    }

    uint8_t iv_buf[16];
    int iv_len = 0;
    if (iv_hex != NULL) {
        iv_len = hex_to_bytes(iv_hex, iv_buf, sizeof(iv_buf));
        if (iv_len != 16) {
            shell_error(sh, "Invalid IV hex string (expected 32 hex chars)");
            return -EINVAL;
        }
    }

    uint8_t ct_buf[1024];
    int ct_len = hex_to_bytes(ct_hex, ct_buf, sizeof(ct_buf));
    if (ct_len <= 0) {
        shell_error(sh, "Invalid ciphertext hex string");
        return -EINVAL;
    }

    if (alg_needs_aligned_input(alg) && (ct_len % 16) != 0) {
        shell_error(sh, "%s mode requires ciphertext length multiple of 16", mode_str);
        return -EINVAL;
    }

    /* Import key */
    psa_key_id_t key_id;
    psa_status_t status = import_aes_key(key_buf, (size_t)key_len, alg,
                                          PSA_KEY_USAGE_DECRYPT, &key_id);
    if (status != PSA_SUCCESS) {
        shell_error(sh, "Key import failed: %d", status);
        return -EIO;
    }

    /* Decrypt using multipart API */
    size_t outbuf_sz = (size_t)ct_len + 16;
    uint8_t *output = (uint8_t *)k_malloc(outbuf_sz);
    if (!output) {
        shell_error(sh, "Failed to allocate output buffer");
        psa_destroy_key(key_id);
        return -ENOMEM;
    }
    size_t out_len;

    if (alg_needs_iv(alg)) {
        psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
        status = psa_cipher_decrypt_setup(&op, key_id, alg);
        if (status == PSA_SUCCESS) {
            status = psa_cipher_set_iv(&op, iv_buf, 16);
        }
        if (status == PSA_SUCCESS) {
            status = psa_cipher_update(&op, ct_buf, (size_t)ct_len,
                                        output, outbuf_sz, &out_len);
        }
        size_t finish_len = 0;
        if (status == PSA_SUCCESS) {
            status = psa_cipher_finish(&op, output + out_len,
                                        outbuf_sz - out_len, &finish_len);
        }
        if (status != PSA_SUCCESS) {
            psa_cipher_abort(&op);
            shell_error(sh, "Decrypt failed: %d", status);
            psa_destroy_key(key_id);
            k_free(output);
            return -EIO;
        }
        out_len += finish_len;
    } else {
        status = psa_cipher_decrypt(key_id, alg,
                                    ct_buf, (size_t)ct_len,
                                    output, outbuf_sz, &out_len);
        if (status != PSA_SUCCESS) {
            shell_error(sh, "Decrypt failed: %d", status);
            psa_destroy_key(key_id);
            k_free(output);
            return -EIO;
        }
    }

    psa_destroy_key(key_id);

    shell_print(sh, "Plaintext (%zu bytes):", out_len);
    shell_hexdump(sh, output, out_len);

    k_free(output);
    return 0;
}

/** aes bench <mode> <key_bits> <payload_kb> */
static int cmd_aes_bench(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 4) {
        shell_error(sh, "Usage: aes bench <ecb|cbc|ctr> <key_bits> <payload_kb>");
        return -EINVAL;
    }

    const char *mode_str = argv[1];
    long key_bits = shell_strtol(argv[2], 10, NULL);
    long payload_kb = shell_strtol(argv[3], 10, NULL);

    psa_algorithm_t alg = mode_to_alg(mode_str);
    if (alg == PSA_ALG_NONE) {
        shell_error(sh, "Unknown mode '%s'", mode_str);
        return -EINVAL;
    }
    if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
        shell_error(sh, "Invalid key size");
        return -EINVAL;
    }
    if (payload_kb <= 0 || payload_kb > 4096) {
        shell_error(sh, "Payload size must be 1..4096 KB");
        return -EINVAL;
    }

    size_t payload_size = (size_t)payload_kb * 1024;

    /* Align to 16 bytes for block modes */
    if (alg_needs_aligned_input(alg)) {
        payload_size = (payload_size / 16) * 16;
        if (payload_size == 0) payload_size = 16;
    }

    /* Generate a deterministic key */
    uint8_t key_buf[32];
    memset(key_buf, 0xAB, sizeof(key_buf));

    /* Allocate and fill payload */
    uint8_t *pt_buf = (uint8_t *)k_malloc(payload_size);
    if (!pt_buf) {
        shell_error(sh, "Failed to allocate %zu bytes", payload_size);
        return -ENOMEM;
    }
    memset(pt_buf, 0xCD, payload_size);

    /* Allocate output buffer */
    uint8_t *out_buf = (uint8_t *)k_malloc(payload_size + 16);
    if (!out_buf) {
        k_free(pt_buf);
        shell_error(sh, "Failed to allocate output buffer");
        return -ENOMEM;
    }

    /* Import key */
    psa_key_id_t key_id;
    psa_status_t status = import_aes_key(key_buf, (size_t)(key_bits / 8), alg,
                                          PSA_KEY_USAGE_ENCRYPT, &key_id);
    if (status != PSA_SUCCESS) {
        shell_error(sh, "Key import failed: %d", status);
        k_free(pt_buf);
        k_free(out_buf);
        return -EIO;
    }

    /* Benchmark: run encrypt and measure time */
    uint8_t iv[16];
    memset(iv, 0, 16);

    int64_t start_us = k_ticks_to_us_floor64(k_uptime_ticks());

    if (alg_needs_iv(alg)) {
        psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
        status = psa_cipher_encrypt_setup(&op, key_id, alg);
        if (status == PSA_SUCCESS) status = psa_cipher_set_iv(&op, iv, 16);
        size_t out_len = 0;
        if (status == PSA_SUCCESS) {
            status = psa_cipher_update(&op, pt_buf, payload_size,
                                        out_buf, payload_size + 16, &out_len);
        }
        size_t finish_len = 0;
        if (status == PSA_SUCCESS) {
            status = psa_cipher_finish(&op, out_buf + out_len,
                                        payload_size + 16 - out_len, &finish_len);
        }
        if (status != PSA_SUCCESS) {
            psa_cipher_abort(&op);
        }
    } else {
        status = psa_cipher_encrypt(key_id, alg,
                                    pt_buf, payload_size,
                                    out_buf, payload_size + 16, NULL);
    }

    int64_t end_us = k_ticks_to_us_floor64(k_uptime_ticks());
    int64_t elapsed_us = end_us - start_us;

    psa_destroy_key(key_id);

    if (status != PSA_SUCCESS) {
        shell_error(sh, "Benchmark encrypt failed: %d", status);
        k_free(pt_buf);
        k_free(out_buf);
        return -EIO;
    }

    if (elapsed_us <= 0) elapsed_us = 1;
    uint32_t throughput_kbps = (uint32_t)((uint64_t)payload_size * 1000000ULL / (uint64_t)elapsed_us / 1024ULL);

    shell_print(sh, "Mode        : AES-%s-%ld", mode_str, key_bits);
#ifdef PSA_CRYPTO_DRIVER_QCC74X_AES
    shell_print(sh, "HW accel    : enabled");
#else
    shell_print(sh, "HW accel    : disabled (SW fallback)");
#endif
    shell_print(sh, "Payload     : %ld KB (%zu bytes)", payload_kb, payload_size);
    shell_print(sh, "Elapsed     : %lld us", (long long)elapsed_us);
    shell_print(sh, "Throughput  : %u KB/s", throughput_kbps);

    k_free(pt_buf);
    k_free(out_buf);

    return 0;
}

/* =========================================================================
 * Shell command registration
 * ======================================================================= */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_aes_commands,
    SHELL_CMD_ARG(selftest, NULL,
                  "Run AES NIST KAT selftest for all modes.\n"
                  "Usage: aes selftest\n",
                  cmd_aes_selftest, 1, 0),

    SHELL_CMD_ARG(info, NULL,
                  "Display QCC74x AES capability info.\n"
                  "Usage: aes info\n",
                  cmd_aes_info, 1, 0),

    SHELL_CMD_ARG(encrypt, NULL,
                  "Encrypt plaintext with AES.\n"
                  "Usage: aes encrypt <ecb|cbc|ctr> <128|192|256> <hex_key> [hex_iv] <hex_plain>\n"
                  "  ecb:  aes encrypt ecb 128 <key_16bytes_hex> <plain_hex>\n"
                  "  cbc:  aes encrypt cbc 256 <key_32bytes_hex> <iv_16bytes_hex> <plain_hex>\n"
                  "  ctr:  aes encrypt ctr 128 <key_16bytes_hex> <iv_16bytes_hex> <plain_hex>\n",
                  cmd_aes_encrypt, 4, 2),

    SHELL_CMD_ARG(decrypt, NULL,
                  "Decrypt ciphertext with AES.\n"
                  "Usage: aes decrypt <ecb|cbc|ctr> <128|192|256> <hex_key> [hex_iv] <hex_cipher>\n",
                  cmd_aes_decrypt, 4, 2),

    SHELL_CMD_ARG(bench, NULL,
                  "Benchmark AES encryption throughput.\n"
                  "Usage: aes bench <ecb|cbc|ctr> <128|192|256> <payload_kb>\n"
                  "  payload_kb: payload size in KB (1..4096)\n",
                  cmd_aes_bench, 4, 0),

    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(aes, &sub_aes_commands, "QCC74x PSA AES commands (encrypt/decrypt/selftest/bench)", NULL);