/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Security Access (SID 0x27) with REAL crypto: the key is derived from
 *        the issued seed with AES-128-CMAC from a vetted library (mbedTLS by
 *        default, or wolfSSL via `make CRYPTO=wolfssl`), plugged into udslib's
 *        `fn_security_seed` / `fn_security_key` hooks.
 *
 * This answers the request in #48 ("0x27 SecurityAccess: derive the key from a
 * seed with AES-CBC-CMAC-128, with two key sets for two security levels — can
 * you make an example?"). The seed -> key relation is exactly
 *
 *     key = AES-128-CMAC(level_secret, seed)
 *
 * and it is the production-shaped sibling of `examples/pro_flash_tool`, which
 * uses a non-cryptographic seed^0xFF placeholder to stay dependency-free.
 *
 * The split of responsibilities is the whole point, and it is unchanged:
 *
 *   - udslib owns the 0x27 PLUMBING: the requestSeed/sendKey state machine, seed
 *     caching, per-level sequencing (NRC 0x24 on an out-of-order key), the
 *     failed-attempt counter and the lockout delay timer (NRC 0x37/0x36), NRC
 *     framing, and gating services by `security_level`. It bundles NO cipher.
 *   - The application (this file) owns the CRYPTO, behind the two hooks. On a
 *     real ECU each `level_secret` is NOT in flash: it is a key handle inside
 *     the SHE/HSM the CPU cannot read, and `aes_cmac()` becomes an HSM call.
 *     Swapping mbedTLS for wolfSSL or an HSM touches only `aes_cmac()` below,
 *     never the library.
 *
 * The ONLY library-specific code is the include block below and the body of
 * `aes_cmac()`. Both back-ends produce a byte-identical CMAC, so the rest of the
 * example — and the library — is unchanged either way. Build needs the matching
 * dev package (`-lmbedcrypto` or `-lwolfssl`); see the Makefile.
 */

#include "uds/uds_core.h"

#if defined(USE_WOLFSSL)
#include "wolfssl/options.h"
#include "wolfssl/wolfcrypt/cmac.h"
#else
#include "mbedtls/cipher.h"
#include "mbedtls/cmac.h"
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>

#define SEED_LEN 16u /* one AES block */
#define KEY_LEN 16u  /* AES-128-CMAC tag length */

static uint8_t g_tx[256];
static uint8_t g_rx[256];

/*
 * Two independent 16-byte secrets, one per security level, mirroring the two
 * key sets in the request. On a real ECU these are NOT in flash: each is a key
 * handle inside the SHE/HSM, and `aes_cmac()` becomes an HSM call against it.
 */
static const uint8_t k_secret_l1[16] = {0x3D, 0x2E, 0x6D, 0xE2, 0xA1, 0x25, 0x17, 0xBA,
                                        0xC5, 0xB3, 0x1B, 0xBD, 0x0E, 0x7E, 0x3B, 0x54};
static const uint8_t k_secret_l2[16] = {0xD0, 0xDF, 0xAA, 0x15, 0x88, 0xE0, 0x4B, 0x5B,
                                        0x14, 0xCE, 0x83, 0x4E, 0x65, 0xE6, 0x21, 0xCD};

static const uint8_t *secret_for_level(uint8_t level)
{
    return (level == 2u) ? k_secret_l2 : k_secret_l1;
}

/* ---- The one and only crypto call site. Swap this for another lib / HSM. ---- */
#if defined(USE_WOLFSSL)
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[KEY_LEN])
{
    word32 out_len = KEY_LEN;
    /* wolfSSL must be built with WOLFSSL_CMAC + WOLFSSL_AES_DIRECT. */
    return wc_AesCmacGenerate(out, &out_len, msg, (word32) msg_len, key, 16u);
}
#else
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[KEY_LEN])
{
    const mbedtls_cipher_info_t *ci = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (ci == NULL) {
        return -1;
    }
    /* keylen is in BITS for the mbedTLS CMAC one-shot. */
    return mbedtls_cipher_cmac(ci, key, 128u, msg, msg_len, out);
}
#endif

/* Constant-time key compare: do not leak how many bytes matched. */
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0u;
    for (size_t i = 0u; i < len; i++) {
        diff |= (uint8_t) (a[i] ^ b[i]);
    }
    return diff == 0u;
}

static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

static int on_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    printf("  <- response:");
    for (uint16_t i = 0u; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");
    return 0;
}

/*
 * requestSeed hook. The library calls this for an odd sub-function (0x01 -> L1,
 * 0x03 -> L2, ...); write up to `max_len` seed bytes into `seed_buf` and return
 * the count. The library caches the seed and hands the same bytes back to
 * `on_security_key` so we never have to track it ourselves.
 *
 * The seed is a per-attempt nonce. It is fixed here for deterministic output; in
 * production fill it from your TRNG / HSM, e.g.
 * mbedtls_ctr_drbg_random(&drbg, seed_buf, SEED_LEN). A fixed seed makes the
 * seed -> key relation replayable, which is exactly what you must NOT ship.
 */
static int on_security_seed(uds_ctx_t *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len)
{
    (void) ctx;
    if (max_len < SEED_LEN) {
        return -(int) 0x22; /* conditionsNotCorrect: tx buffer too small for seed */
    }
    /* Vary the fixed seed by level so the two levels are visibly distinct. */
    for (uint8_t i = 0u; i < SEED_LEN; i++) {
        seed_buf[i] = (uint8_t) ((level << 4) ^ (0xA0u + i));
    }
    return (int) SEED_LEN;
}

/*
 * sendKey hook. `seed` is the exact seed the library issued for this level (no
 * caching needed on our side); `key`/`key_len` is what the tester sent. Derive
 * the expected key with AES-128-CMAC over the seed and compare in constant time.
 * Return 0 to unlock (library sets ctx.security.level = level), or a negative
 * NRC. The library handles the attempt counter and lockout delay on failure.
 */
static int on_security_key(uds_ctx_t *ctx, uint8_t level, const uint8_t *seed, const uint8_t *key,
                           uint16_t key_len)
{
    (void) ctx;
    uint8_t expected[KEY_LEN];

    if (key_len != KEY_LEN) {
        return -(int) 0x35; /* invalidKey: wrong length is a wrong key */
    }
    if (aes_cmac(secret_for_level(level), seed, SEED_LEN, expected) != 0) {
        return -(int) 0x22; /* conditionsNotCorrect: crypto failure */
    }
    if (!ct_equal(key, expected, KEY_LEN)) {
        return -(int) 0x35; /* invalidKey (ISO 14229-1) */
    }
    return 0; /* unlock this level */
}

/* A vendor service that must only run once security level 1 is unlocked. The
 * library enforces this via the service entry's security_mask (NRC 0x33
 * otherwise); the handler never even runs until ctx.security.level >= 1. */
static int handle_secure_op(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    ctx->config->tx_buffer[1] = 0xAC; /* "action done" marker */
    return uds_send_response(ctx, 2u);
}

static const uds_service_entry_t user_services[] = {
    {0xBAu, 1u, UDS_SESSION_ALL, 1u, handle_secure_op, NULL, 0u}, /* security_mask 1 => needs L1 */
};

/* The tester side: compute the key the ECU expects for `level` over `seed`. */
static void client_compute_key(uint8_t level, const uint8_t *seed, uint8_t out[KEY_LEN])
{
    aes_cmac(secret_for_level(level), seed, SEED_LEN, out);
}

static const uint8_t *g_last_response;

/* Capture the response so the "client" can read the seed back out of 67 xx ... */
static int on_tp_send_capture(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    g_last_response = data;
    return on_tp_send(ctx, data, len);
}

static void send_request(uds_ctx_t *ctx, const uint8_t *req, uint16_t len)
{
    printf("-> request:");
    for (uint16_t i = 0u; i < len; i++) {
        printf(" %02X", req[i]);
    }
    printf("\n");
    uds_input_sdu(ctx, req, len);
}

int main(void)
{
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = get_time_ms;
    cfg.fn_tp_send = on_tp_send_capture;
    cfg.rx_buffer = g_rx;
    cfg.rx_buffer_size = sizeof(g_rx);
    cfg.tx_buffer = g_tx;
    cfg.tx_buffer_size = sizeof(g_tx);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.fn_security_seed = on_security_seed; /* <- requestSeed wiring */
    cfg.fn_security_key = on_security_key;   /* <- sendKey wiring     */
    cfg.security_max_attempts = 3u;          /* lock out after 3 bad keys */
    cfg.security_delay_ms = 10000u;          /* ...for 10 s (ISO 14229 C-15) */
    cfg.user_services = user_services;
    cfg.user_service_count = 1u;

    uds_ctx_t ctx;
    if (uds_init(&ctx, &cfg) != UDS_OK) {
        printf("uds_init failed\n");
        return 1;
    }

    uint8_t seed[SEED_LEN];
    uint8_t key[KEY_LEN];
    uint8_t req[2u + KEY_LEN];

    printf("=== 1. Gated service 0xBA while LOCKED -> NRC 0x33 (securityAccessDenied) ===\n");
    const uint8_t gated[] = {0xBA};
    send_request(&ctx, gated, sizeof(gated)); /* 7F BA 33 */

    printf("\n=== 2. requestSeed level 1 (0x27 0x01) ===\n");
    const uint8_t req_seed_l1[] = {0x27, 0x01};
    send_request(&ctx, req_seed_l1, sizeof(req_seed_l1)); /* 67 01 <16-byte seed> */
    memcpy(seed, &g_last_response[2], SEED_LEN);          /* client reads the seed back */

    printf("\n=== 3. sendKey level 1 with AES-128-CMAC(secret_l1, seed) -> unlock ===\n");
    client_compute_key(1u, seed, key);
    req[0] = 0x27;
    req[1] = 0x02;
    memcpy(&req[2], key, KEY_LEN);
    send_request(&ctx, req, sizeof(req)); /* 67 02 */
    printf("   ctx.security.level = %u\n", ctx.security.level);

    printf("\n=== 4. Gated service 0xBA after unlock -> allowed ===\n");
    send_request(&ctx, gated, sizeof(gated)); /* FA AC */

    printf("\n=== 5. A WRONG key on level 2 -> NRC 0x35, attempt counter increments ===\n");
    const uint8_t req_seed_l2[] = {0x27, 0x03};
    send_request(&ctx, req_seed_l2, sizeof(req_seed_l2)); /* 67 03 <16-byte seed> */
    memcpy(seed, &g_last_response[2], SEED_LEN);
    client_compute_key(2u, seed, key);
    req[1] = 0x04;
    memcpy(&req[2], key, KEY_LEN);
    req[2] ^= 0xFFu;                      /* corrupt one byte of the key */
    send_request(&ctx, req, sizeof(req)); /* 7F 27 35 */

    printf("\n=== 6. Correct key on level 2 -> unlock level 2 ===\n");
    /* The failed key did not consume the armed seed, but re-requesting one is
     * the safe, portable retry pattern (don't assume the server keeps it). */
    send_request(&ctx, req_seed_l2, sizeof(req_seed_l2));
    memcpy(seed, &g_last_response[2], SEED_LEN);
    client_compute_key(2u, seed, key);
    req[1] = 0x04;
    memcpy(&req[2], key, KEY_LEN);
    send_request(&ctx, req, sizeof(req)); /* 67 04 */
    printf("   ctx.security.level = %u\n", ctx.security.level);

    return 0;
}
