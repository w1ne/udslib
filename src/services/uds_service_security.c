/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_security.c
 * @brief Security Access (0x27) & Authentication (0x29)
 */

#include <string.h>

#include "uds_internal.h"

void uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                         uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint32_t now = ctx->config->get_time_ms();

    /* C-14: Security Delay Timer Check */
    if (ctx->security.delay_end != 0u) {
        if (now < ctx->security.delay_end) {
            uds_nrc(out, UDS_NRC_REQUIRED_TIME_DELAY);
            return;
        }
        /* Delay expired, reset counter if we want, but ISO says just allow next attempt */
        ctx->security.delay_end = 0u;
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);

    if ((sub % 2u) != 0u) { /* Request Seed (Odd subfunctions: 0x01, 0x03...) */
        if (ctx->config->fn_security_seed == NULL) {
            uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
            return;
        }

        uint8_t level = (uint8_t) (((uint16_t) sub + 1u) / 2u);

        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = sub_raw;

        int seed_len = ctx->config->fn_security_seed(ctx, level, &ctx->config->tx_buffer[2],
                                                     (uint16_t) (ctx->config->tx_buffer_size - 2u));
        if (seed_len < 0) {
            uds_nrc(out, (uint8_t) - (int32_t) seed_len);
            return;
        }

        /* Cache the issued seed so the key verifier can validate against it,
           and arm the requestSeed -> sendKey sequence for this level. */
        uint8_t cache_len =
            (seed_len > (int) UDS_SECURITY_SEED_MAX) ? UDS_SECURITY_SEED_MAX : (uint8_t) seed_len;
        memcpy(ctx->security.seed, &ctx->config->tx_buffer[2], cache_len);
        ctx->security.seed_len = cache_len;
        ctx->security.seed_level = level;

        uds_ok(out, (uint16_t) ((uint16_t) seed_len + 2u));
        return;
    }
    else { /* Send Key (Even subfunctions: 0x02, 0x04...) */
        if (ctx->config->fn_security_key == NULL) {
            uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
            return;
        }

        uint8_t level = (uint8_t) (sub / 2u);

        /* ISO 14229-1: a key must be preceded by a requestSeed for the same
           level, otherwise it is an out-of-sequence request. */
        if (ctx->security.seed_level == 0u || ctx->security.seed_level != level) {
            uds_nrc(out, UDS_NRC_REQUEST_SEQUENCE_ERROR);
            return;
        }

        /* Standard assumes key is passed in the request after SID and subfn */
        int res = ctx->config->fn_security_key(ctx, level, ctx->security.seed, &data[2],
                                               (uint16_t) (len - 2u));
        if (res == 0) {
            /* Success! Reset attempts and consume the outstanding seed. */
            ctx->security.attempts = 0u;
            ctx->security.seed_level = 0u;
            ctx->security.seed_len = 0u;
            ctx->security.level = level;

            /* NVM Persistence: Save State */
            if (ctx->config->fn_nvm_save) {
                uint8_t state[2] = {ctx->session.active, ctx->security.level};
                ctx->config->fn_nvm_save(ctx, state, 2u);
            }

            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sub_raw;
            uds_ok(out, 2u);
            return;
        }
        else {
            /* C-15: Attempt Management */
            ctx->security.attempts++;
            uint8_t max_att =
                ctx->config->security_max_attempts ? ctx->config->security_max_attempts : 3u;

            if (ctx->security.attempts >= max_att) {
                uint32_t delay =
                    ctx->config->security_delay_ms ? ctx->config->security_delay_ms : 10000u;
                ctx->security.delay_end = now + delay;
                uds_nrc(out, UDS_NRC_EXCEEDED_ATTEMPTS);
                return;
            }

            /* Return NRC provided by key handler, or 0x35 (InvalidKey) */
            uint8_t nrc = (res < 0) ? (uint8_t) - (int32_t) res : UDS_NRC_INVALID_KEY;
            uds_nrc(out, nrc);
            return;
        }
    }
}

/* AuthenticationReturnParameter (ISO 14229-1:2020) values the library emits. */
#define UDS_ARP_DEAUTHENTICATED 0x10u

void uds_internal_handle_authentication(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                        uds_result_t *out)
{
    /* Sub-function (0x00-0x08) is validated by the dispatcher (mask_sub_29).
     * Authentication always responds (suppressPosRsp is not used for 0x29). */
    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);
    /* 0x29 never suppresses: override the flag set by the dispatcher so that
     * execute_handler always emits the positive response. */
    ctx->scratch.suppress_pos_resp = false;

    uint8_t *tx = ctx->config->tx_buffer;

    /* deAuthenticate (0x00): handled natively, clears the authenticated state. */
    if (sub == UDS_AUTH_DEAUTHENTICATE) {
        ctx->security.authenticated = false;
        tx[0] = (uint8_t) (UDS_SID_AUTHENTICATION + UDS_RESPONSE_OFFSET);
        tx[1] = sub;
        tx[2] = UDS_ARP_DEAUTHENTICATED;
        uds_ok(out, 3u);
        return;
    }

    /* authenticationConfiguration (0x08): report the configured method. */
    if (sub == UDS_AUTH_CONFIGURATION) {
        tx[0] = (uint8_t) (UDS_SID_AUTHENTICATION + UDS_RESPONSE_OFFSET);
        tx[1] = sub;
        tx[2] = ctx->config->auth_configuration;
        uds_ok(out, 3u);
        return;
    }

    /* 0x01-0x07: certificate / proof / challenge — the crypto is delegated. */
    if (ctx->config->fn_auth == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    uint8_t *out_payload = &tx[2];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    int written =
        ctx->config->fn_auth(ctx, sub, &data[2], (uint16_t) (len - 2u), out_payload, max_payload);

    if (written < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) written);
        return;
    }

    tx[0] = (uint8_t) (UDS_SID_AUTHENTICATION + UDS_RESPONSE_OFFSET);
    tx[1] = data[1];
    uds_ok(out, (uint16_t) ((uint16_t) written + 2u));
}
