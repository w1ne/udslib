/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_session.c
 * @brief Diagnostic Session Control (0x10) & Tester Present (0x3E)
 */

#include "uds_internal.h"

void uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                         uds_result_t *out)
{
    (void) len;
    uint8_t sub = (uint8_t) (data[1] & UDS_MASK_SUBFUNCTION);

    /* C-01: Validate Session ID */
    if (sub != UDS_SESSION_ID_DEFAULT && sub != UDS_SESSION_ID_PROGRAMMING &&
        sub != UDS_SESSION_ID_EXTENDED && sub != UDS_SESSION_ID_SAFETY) {
        uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    /* Optional OEM transition policy. ISO 14229-1 itself permits any
     * session-to-session transition; an application that needs a restricted
     * graph (e.g. extended-before-programming) supplies this hook. A rejected
     * transition leaves the active session unchanged. */
    if (ctx->config->fn_session_transition_allowed != NULL &&
        !ctx->config->fn_session_transition_allowed(ctx, ctx->session.active, sub)) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    /* C-06: Security Reset on Session Transition */
    /* Security shall be re-locked when transitioning from one session to another (or same) */
    /* Note: "Same" session transition usually also resets. ISO says (re-)initialize. */
    if (ctx->session.active != sub || sub == UDS_SESSION_ID_DEFAULT) {
        ctx->security.level = 0u;
        ctx->security.authenticated = false;
        /* Drop any outstanding seed: the sequence restarts with the session. */
        ctx->security.seed_level = 0u;
        ctx->security.seed_len = 0u;
    }

    /* ISO 14229-1: entering the default session restores communication and DTC
     * setting to their defaults (normal Rx/Tx enabled, DTC setting on). A tester
     * that disabled either via 0x28 / 0x85 for a reprogramming sequence need not
     * explicitly re-enable them — returning to the default session does. */
    if (sub == UDS_SESSION_ID_DEFAULT) {
        ctx->session.comm_state = 0x00u;        /* enable Rx/Tx */
        ctx->session.dtc_setting_disabled = 0u; /* DTC setting on */
    }

    /* Update Active Session */
    ctx->session.active = sub;

    /* Prepare Response */
    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_SESSION_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;

    /* C-19: Advertise P2/P2* from the single resolved source (ctx->session).
     * Values were resolved exactly once by uds_init; do not re-read config. */
    uint16_t p2 = ctx->session.p2_ms;
    uint16_t p2_star_val = (uint16_t) (ctx->session.p2_star_ms / 10u); /* ISO unit: 10ms */

    ctx->config->tx_buffer[2] = (uint8_t) ((p2 >> 8) & 0xFFu);
    ctx->config->tx_buffer[3] = (uint8_t) (p2 & 0xFFu);
    ctx->config->tx_buffer[4] = (uint8_t) ((p2_star_val >> 8) & 0xFFu);
    ctx->config->tx_buffer[5] = (uint8_t) (p2_star_val & 0xFFu);

    /* NVM Persistence: Save State on Change */
    if (ctx->config->fn_nvm_save != NULL) {
        uint8_t state[2] = {ctx->session.active, ctx->security.level};
        ctx->config->fn_nvm_save(ctx, state, 2u);
    }

    uds_ok(out, 6u);
}

void uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                        uds_result_t *out)
{
    (void) len;
    uint8_t sub = (uint8_t) (data[1] & UDS_MASK_SUBFUNCTION);
    if (sub == 0x00u) {
        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TESTER_PRESENT + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = 0x00u;
        uds_ok(out, 2u);
        return;
    }
    uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
}
