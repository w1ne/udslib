/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Worked example: wire the Authentication service (SID 0x29) to a
 *        challenge / proof-of-ownership flow via the `fn_auth` hook.
 *
 * udslib's 0x29 handler is transport/sub-function plumbing only: it validates
 * the request, calls `fn_auth`, and frames the response. The authentication
 * LOGIC and the cryptography live in the application (or its HSM) behind that
 * hook. The library deliberately bundles no cipher — on a real ECU the key
 * sits in an SHE/HSM the software can't read, and a hook is the only thing
 * that works there. (The 0x84 SecuredDataTransmission hooks follow the exact
 * same pattern.)
 *
 * This example implements a minimal requestChallenge -> proofOfOwnership ->
 * grant flow so you can see where your crypto plugs in.
 *
 * >>> The keyed tag below (demo_tag) is NOT cryptography. It exists only to
 * >>> make the wiring runnable and deterministic. In production, replace
 * >>> demo_tag() with an AES-CMAC / HMAC from a vetted library or your HSM.
 */

#include "uds/uds_core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SUB_VERIFY_CERT_UNI 0x01u /* deAuthenticate (0x00) is handled natively */
#define SUB_PROOF_OF_OWNERSHIP 0x03u
#define SUB_REQUEST_CHALLENGE 0x05u

/* ISO 14229-1:2020 AuthenticationReturnParameter values. */
#define AUTH_RET_GENERAL_REJECT 0x00u
#define AUTH_RET_OWNERSHIP_VERIFIED 0x02u
#define AUTH_RET_DEAUTHENTICATED 0x10u
#define AUTH_RET_CHALLENGE 0x11u /* requestChallenge accepted, challenge follows */

static uint8_t g_tx[256];
static uint8_t g_rx[256];

/* Application authentication state (would live in the ECU app / HSM). */
static const uint8_t k_secret_key[4] = {0xDE, 0xAD, 0xBE, 0xEF};
static uint8_t g_challenge[4];

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

/* DEMO ONLY keyed tag over (key || data). NOT secure. Swap for AES-CMAC/HMAC. */
static uint32_t demo_tag(const uint8_t *data, uint16_t len)
{
    uint32_t h = 0x811C9DC5u; /* FNV-1a offset basis */
    for (uint16_t i = 0u; i < sizeof(k_secret_key); i++) {
        h = (h ^ k_secret_key[i]) * 0x01000193u;
    }
    for (uint16_t i = 0u; i < len; i++) {
        h = (h ^ data[i]) * 0x01000193u;
    }
    return h;
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
             * crypto lib / HSM). On success, report the evaluation status. */
            out[0] = 0x01; /* certificateVerified */
            return 1;

        case SUB_REQUEST_CHALLENGE: {
            /* Issue a challenge nonce (fixed here for deterministic output;
             * use your RNG / HSM in production). */
            g_challenge[0] = 0x01;
            g_challenge[1] = 0x02;
            g_challenge[2] = 0x03;
            g_challenge[3] = 0x04;
            out[0] = AUTH_RET_CHALLENGE;
            memcpy(&out[1], g_challenge, sizeof(g_challenge));
            return (int) (1 + sizeof(g_challenge)); /* return param + challenge */
        }

        case SUB_PROOF_OF_OWNERSHIP: {
            /* Client must return the tag over the issued challenge. */
            uint32_t expected = demo_tag(g_challenge, sizeof(g_challenge));
            if (len < 4u) {
                return -(int) 0x13; /* incorrectMessageLength */
            }
            uint32_t got = (uint32_t) (((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) |
                                       ((uint32_t) data[2] << 8) | (uint32_t) data[3]);
            if (got != expected) {
                ctx->authenticated = false;
                return -(int) 0x34; /* authenticationFailed (ISO 14229-1:2020) */
            }
            /* Mark the channel authenticated. The library auto-clears this on
             * deAuthenticate, session change, S3 timeout, and reset, and gates
             * services selected by the fn_auth_required hook. */
            ctx->authenticated = true;
            out[0] = AUTH_RET_OWNERSHIP_VERIFIED;
            return 1;
        }

        default:
            return -(int) 0x12; /* subFunctionNotSupported */
    }
}

/* A vendor service that must only run on an authenticated channel. The library
 * enforces this via the fn_auth_required hook (NRC 0x34 otherwise); the handler
 * never even runs until ctx.authenticated is set. */
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
    send_request(&ctx, req_challenge, sizeof(req_challenge)); /* 69 05 11 01 02 03 04 */

    printf("\n=== 4. proofOfOwnership with the correct tag -> verified ===\n");
    uint32_t tag = demo_tag(g_challenge, sizeof(g_challenge));
    const uint8_t proof_ok[] = {0x29,
                                SUB_PROOF_OF_OWNERSHIP,
                                (uint8_t) (tag >> 24),
                                (uint8_t) (tag >> 16),
                                (uint8_t) (tag >> 8),
                                (uint8_t) tag};
    send_request(&ctx, proof_ok, sizeof(proof_ok)); /* 69 03 02 */
    printf("   ctx.authenticated = %s\n", ctx.authenticated ? "true" : "false");

    printf("\n=== 5. Gated service 0xBA AFTER auth -> allowed ===\n");
    send_request(&ctx, gated, sizeof(gated)); /* FA AC */

    return 0;
}
