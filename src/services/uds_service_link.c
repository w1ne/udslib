/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_link.c
 * @brief Reprogramming-negotiation services: LinkControl (0x87),
 *        AccessTimingParameter (0x83).
 */

#include "uds_internal.h"

void uds_internal_handle_link_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);

    if (ctx->config->fn_link_control == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (sub == 0x01u || sub == 0x02u) {
        /* verifyModeTransition: decode the requested link parameter. */
        uint32_t param;
        if (sub == 0x01u) {
            if (len < 3u) {
                uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
                return;
            }
            param = (uint32_t) data[2]; /* linkControlModeIdentifier */
        }
        else {
            if (len < 5u) {
                uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
                return;
            }
            param = ((uint32_t) data[2] << 16u) | ((uint32_t) data[3] << 8u) |
                    (uint32_t) data[4]; /* 3-byte baud rate */
        }

        int res = ctx->config->fn_link_control(ctx, sub, param);
        if (res != 0) {
            uds_nrc(out, (uint8_t) - (int32_t) res);
            return;
        }

        ctx->link_ctrl_verified = true;
        ctx->link_ctrl_param = param;

        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_LINK_CONTROL + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = sub;
        uds_ok(out, 2u);
        return;
    }

    /* sub == 0x03u transitionMode (sub-mask rejects any other subfunction). */
    if (!ctx->link_ctrl_verified) {
        uds_nrc(out, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        return;
    }

    int res = ctx->config->fn_link_control(ctx, sub, ctx->link_ctrl_param);
    if (res != 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    ctx->link_ctrl_verified = false;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_LINK_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    uds_ok(out, 2u);
}

void uds_internal_handle_access_timing(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                       uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);

    switch (sub) {
        case 0x01u:   /* readExtendedTimingParameterSet */
        case 0x03u: { /* readCurrentlyActiveTimingParameters */
            uint16_t p2 = ctx->p2_ms;
            uint16_t p2_star = (uint16_t) (ctx->p2_star_ms / 10u); /* P2* resolution is 10ms */
            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ACCESS_TIMING + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sub;
            ctx->config->tx_buffer[2] = (uint8_t) ((p2 >> 8u) & 0xFFu);
            ctx->config->tx_buffer[3] = (uint8_t) (p2 & 0xFFu);
            ctx->config->tx_buffer[4] = (uint8_t) ((p2_star >> 8u) & 0xFFu);
            ctx->config->tx_buffer[5] = (uint8_t) (p2_star & 0xFFu);
            uds_ok(out, 6u);
            return;
        }

        case 0x02u: /* setTimingParametersToDefaultValues */
            ctx->p2_ms = (ctx->config->p2_ms > 0u) ? ctx->config->p2_ms : 50u;
            ctx->p2_star_ms = (ctx->config->p2_star_ms > 0u) ? ctx->config->p2_star_ms : 5000u;
            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ACCESS_TIMING + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sub;
            uds_ok(out, 2u);
            return;

        case 0x04u: { /* setTimingParametersToGivenValues: [P2 hi/lo][P2* hi/lo, 10ms] */
            if (len < 6u) {
                uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
                return;
            }
            ctx->p2_ms = (uint16_t) (((uint16_t) data[2] << 8u) | (uint16_t) data[3]);
            ctx->p2_star_ms = (uint32_t) (((uint16_t) data[4] << 8u) | (uint16_t) data[5]) * 10u;
            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ACCESS_TIMING + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sub;
            uds_ok(out, 2u);
            return;
        }

        default:
            uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
            return;
    }
}
