/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Authentication service (SID 0x29) with REAL crypto: the proof of
 *        ownership is verified with AES-128-CMAC from a vetted library
 *        (mbedTLS by default, or wolfSSL via `make CRYPTO=wolfssl`), plugged
 *        into udslib's `fn_auth` hook.
 *
 * This is the production-shaped sibling of `examples/auth_challenge`, which
 * uses a non-cryptographic placeholder tag to keep its build dependency-free.
 * Here the placeholder is replaced by a vetted library so you can see exactly
 * where and how a real cipher attaches.
 *
 * The split of responsibilities is unchanged, and it is the whole point:
 *
 *   - udslib owns the 0x29 PLUMBING: sub-function validation, the
 *     `ctx.security.authenticated` state machine, NRC framing, and service gating via
 *     `fn_auth_required`. It bundles NO cipher.
 *   - The application (this file) owns the CRYPTO, behind `fn_auth`. On a real
 *     ECU the AES key lives in an SHE/HSM the CPU cannot read, so the CMAC
 *     would be computed by the HSM; swapping mbedTLS for a wolfSSL or HSM call
 *     touches only the `aes_cmac()` helper below, never the library.
 *
 * The ONLY library-specific code is the include block below and the body of
 * `aes_cmac()`. Both back-ends produce a byte-identical CMAC, so the rest of
 * the example — and the library — is unchanged either way. Build needs the
 * matching dev package (`-lmbedcrypto` or `-lwolfssl`); see the Makefile.
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

#define SUB_VERIFY_CERT_UNI 0x01u /* deAuthenticate (0x00) is handled natively */
#define SUB_PROOF_OF_OWNERSHIP 0x03u
#define SUB_REQUEST_CHALLENGE 0x05u

/* ISO 14229-1:2020 AuthenticationReturnParameter values. */
#define AUTH_RET_OWNERSHIP_VERIFIED 0x02u
#define AUTH_RET_CHALLENGE 0x11u /* requestChallenge accepted, challenge follows */

#define CHALLENGE_LEN 16u /* one AES block */
#define CMAC_LEN 16u      /* AES-128-CMAC tag length */

static uint8_t g_tx[256];
static uint8_t g_rx[256];

/*
 * Application authentication state. On a real ECU `k_aes_key` is NOT in flash:
 * it is a key handle inside the SHE/HSM, and `aes_cmac()` becomes an HSM call.
 */
static const uint8_t k_aes_key[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                                      0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
static uint8_t g_challenge[CHALLENGE_LEN];

/* ---- The one and only crypto call site. Swap this for another lib / HSM. ---- */
#if defined(USE_WOLFSSL)
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[CMAC_LEN])
{
    word32 out_len = CMAC_LEN;
    /* wolfSSL must be built with WOLFSSL_CMAC + WOLFSSL_AES_DIRECT. */
    return wc_AesCmacGenerate(out, &out_len, msg, (word32) msg_len, key, 16u);
}
#else
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[CMAC_LEN])
{
    const mbedtls_cipher_info_t *ci = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (ci == NULL) {
        return -1;
    }
    /* keylen is in BITS for the mbedTLS CMAC one-shot. */
    return mbedtls_cipher_cmac(ci, key, 128u, msg, msg_len, out);
}
#endif

/* Constant-time tag compare: do not leak how many bytes matched. */
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
 * The whole 0x29 integration: one callback. `subfn` is the authentication
 * sub-function; `data`/`len` is everything after it; write the response body
 * (after `0x69 <subfn>`) into `out`.
 */
static int handle_auth(uds_ctx_t *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                       uint8_t *out, uint16_t max_len)
{
    (void) max_len;

    switch (subfn) {
        case SUB_VERIFY_CERT_UNI:
            /* Your code verifies the client certificate here (delegate to your
             * crypto lib / HSM: parse the cert, check the chain & signature).
             * On success, report the evaluation status. */
            out[0] = 0x01; /* certificateVerified */
            return 1;

        case SUB_REQUEST_CHALLENGE: {
            /* Issue a challenge nonce. Fixed here for deterministic output; in
             * production fill it from your TRNG / HSM, e.g.
             * mbedtls_ctr_drbg_random(&drbg, g_challenge, CHALLENGE_LEN). */
            for (uint8_t i = 0u; i < CHALLENGE_LEN; i++) {
                g_challenge[i] = (uint8_t) (0x10u + i);
            }
            out[0] = AUTH_RET_CHALLENGE;
            memcpy(&out[1], g_challenge, CHALLENGE_LEN);
            return (int) (1u + CHALLENGE_LEN); /* return param + challenge */
        }

        case SUB_PROOF_OF_OWNERSHIP: {
            /* The client must return AES-128-CMAC_key(challenge). Recompute it
             * and compare in constant time. */
            uint8_t expected[CMAC_LEN];
            if (len < CMAC_LEN) {
                return -(int) 0x13; /* incorrectMessageLength */
            }
            if (aes_cmac(k_aes_key, g_challenge, CHALLENGE_LEN, expected) != 0) {
                return -(int) 0x22; /* conditionsNotCorrect (crypto failure) */
            }
            if (!ct_equal(data, expected, CMAC_LEN)) {
                ctx->security.authenticated = false;
                return -(int) 0x34; /* authenticationFailed (ISO 14229-1:2020) */
            }
            /* Mark the channel authenticated. The library auto-clears this on
             * deAuthenticate, session change, S3 timeout, and reset, and gates
             * services selected by the fn_auth_required hook. */
            ctx->security.authenticated = true;
            out[0] = AUTH_RET_OWNERSHIP_VERIFIED;
            return 1;
        }

        default:
            return -(int) 0x12; /* subFunctionNotSupported */
    }
}

/* A vendor service that must only run on an authenticated channel. The library
 * enforces this via the fn_auth_required hook (NRC 0x34 otherwise); the handler
 * never even runs until ctx.security.authenticated is set. */
static void handle_secure_op(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    ctx->config->tx_buffer[1] = 0xAC; /* "action done" marker */
    uds_ok(out, 2u);
}

static const uds_service_entry_t user_services[] = {
    {0xBAu, 1u, UDS_SESSION_ALL, 0u, handle_secure_op, NULL, 0u},
};

/* Tell the library which SIDs require an authenticated channel. */
static bool auth_required(uds_ctx_t *ctx, uint8_t sid)
{
    (void) ctx;
    return (sid == 0xBAu);
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
    cfg.fn_tp_send = on_tp_send;
    cfg.rx_buffer = g_rx;
    cfg.rx_buffer_size = sizeof(g_rx);
    cfg.tx_buffer = g_tx;
    cfg.tx_buffer_size = sizeof(g_tx);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.fn_auth = handle_auth; /* <- the entire 0x29 wiring */
    cfg.user_services = user_services;
    cfg.user_service_count = 1u;
    cfg.fn_auth_required = auth_required; /* gate 0xBA on authentication */

    uds_ctx_t ctx;
    if (uds_init(&ctx, &cfg) != UDS_OK) {
        printf("uds_init failed\n");
        return 1;
    }

    printf("=== 1. Gated service 0xBA BEFORE auth -> NRC 0x34 (authenticationRequired) ===\n");
    const uint8_t gated[] = {0xBA};
    send_request(&ctx, gated, sizeof(gated)); /* 7F BA 34 */

    printf("\n=== 2. verifyCertificateUnidirectional (0x29 0x01) ===\n");
    const uint8_t verify_cert[] = {0x29, SUB_VERIFY_CERT_UNI, 0xC0, 0xDE};
    send_request(&ctx, verify_cert, sizeof(verify_cert)); /* 69 01 01 */

    printf("\n=== 3. requestChallengeForAuthentication (0x29 0x05) ===\n");
    const uint8_t req_challenge[] = {0x29, SUB_REQUEST_CHALLENGE};
    send_request(&ctx, req_challenge, sizeof(req_challenge)); /* 69 05 11 <16-byte challenge> */

    printf("\n=== 4. proofOfOwnership with real AES-128-CMAC(challenge) -> verified ===\n");
    uint8_t proof[2u + CMAC_LEN];
    proof[0] = 0x29;
    proof[1] = SUB_PROOF_OF_OWNERSHIP;
    /* The client side computes the same CMAC over the challenge it received. */
    if (aes_cmac(k_aes_key, g_challenge, CHALLENGE_LEN, &proof[2]) != 0) {
        printf("client CMAC failed\n");
        return 1;
    }
    send_request(&ctx, proof, sizeof(proof)); /* 69 03 02 */
    printf("   ctx.security.authenticated = %s\n", ctx.security.authenticated ? "true" : "false");

    printf("\n=== 5. proofOfOwnership with a WRONG tag -> NRC 0x34 ===\n");
    uint8_t bad_proof[2u + CMAC_LEN];
    memcpy(bad_proof, proof, sizeof(bad_proof));
    bad_proof[2] ^= 0xFFu; /* corrupt one byte of the tag */
    /* Re-issue a challenge first (proofOfOwnership consumes the live one). */
    send_request(&ctx, req_challenge, sizeof(req_challenge));
    /* recompute against the (identical, fixed) challenge, then corrupt it */
    aes_cmac(k_aes_key, g_challenge, CHALLENGE_LEN, &bad_proof[2]);
    bad_proof[2] ^= 0xFFu;
    send_request(&ctx, bad_proof, sizeof(bad_proof)); /* 7F 29 34 */
    printf("   ctx.security.authenticated = %s\n", ctx.security.authenticated ? "true" : "false");

    printf("\n=== 6. Re-authenticate, then gated service 0xBA -> allowed ===\n");
    send_request(&ctx, req_challenge, sizeof(req_challenge));
    aes_cmac(k_aes_key, g_challenge, CHALLENGE_LEN, &proof[2]);
    send_request(&ctx, proof, sizeof(proof));
    send_request(&ctx, gated, sizeof(gated)); /* FA AC */

    return 0;
}
