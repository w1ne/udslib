/**
 * @file uds_service_session.c
 * @brief Diagnostic Session Control (0x10) & Tester Present (0x3E)
 */

#include "uds_internal.h"

int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1];
    if (sub == 0x03) { /* Extended Session */
        ctx->active_session = 0x03;
        ctx->config->tx_buffer[0] = 0x50;
        ctx->config->tx_buffer[1] = 0x03;
        ctx->config->tx_buffer[2] = 0x00;
        ctx->config->tx_buffer[3] = 0x32;
        ctx->config->tx_buffer[4] = 0x01;
        ctx->config->tx_buffer[5] = 0xF4;
        uds_send_response(ctx, 6);
        uds_internal_log(ctx, UDS_LOG_INFO, "Session changed to Extended");
    } else {
        ctx->active_session = 0x01;
        ctx->config->tx_buffer[0] = 0x50;
        ctx->config->tx_buffer[1] = sub;
        uds_send_response(ctx, 2);
    }
    
    /* NVM Persistence: Save State on Change */
    if (ctx->config->fn_nvm_save) {
        uint8_t state[2] = {ctx->active_session, ctx->security_level};
        ctx->config->fn_nvm_save(ctx, state, 2);
    }

    return UDS_OK;
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
    return -1; /* Subfunction not supported */
}
