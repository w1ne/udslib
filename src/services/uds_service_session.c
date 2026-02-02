/**
 * @file uds_service_session.c
 * @brief Diagnostic Session Control (0x10) & Tester Present (0x3E)
 */

#include "uds_internal.h"

int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1]; // No suppress bit mask for 0x10

    /* C-01: Validate Sub-function */
    if (sub == 0x01 || sub == 0x02 || sub == 0x03) {
        
        /* C-06: Security Reset on Transition */
        if (ctx->active_session != sub) {
            ctx->security_level = 0;
            ctx->active_session = sub;
            uds_internal_log(ctx, UDS_LOG_INFO, "Session Changed. Security Reset.");
        }

        /* C-19: Use Configured Timings (P2 in ms, P2* in 10ms resolution) */
        uint16_t p2 = ctx->config->p2_ms;
        uint16_t p2_star_10ms = ctx->config->p2_star_ms / 10;

        ctx->config->tx_buffer[0] = 0x50;
        ctx->config->tx_buffer[1] = sub;
        ctx->config->tx_buffer[2] = (p2 >> 8) & 0xFF;
        ctx->config->tx_buffer[3] = (p2 & 0xFF);
        ctx->config->tx_buffer[4] = (p2_star_10ms >> 8) & 0xFF;
        ctx->config->tx_buffer[5] = (p2_star_10ms & 0xFF);

        uds_send_response(ctx, 6);

        /* NVM Persistence: Save State on Change */
        if (ctx->config->fn_nvm_save) {
            uint8_t state[2] = {ctx->active_session, ctx->security_level};
            ctx->config->fn_nvm_save(ctx, state, 2);
        }

        return UDS_OK;
    }

    return uds_send_nrc(ctx, 0x10, 0x12); /* SubFunction Not Supported */
}

int uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1];
    if ((sub & 0x7F) == 0x00) {
        if (!(sub & 0x80)) { /* Suppress Pos Response bit */
            ctx->config->tx_buffer[0] = 0x7E;
            ctx->config->tx_buffer[1] = sub & 0x7F;
            uds_send_response(ctx, 2);
        }
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x3E, 0x12); /* Subfunction not supported */
}

