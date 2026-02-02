/**
 * @file uds_service_session.c
 * @brief Diagnostic Session Control (0x10) & Tester Present (0x3E)
 */

#include "uds_internal.h"

int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    if (sub == UDS_SESSION_ID_DEFAULT) { /* Default Session */
        ctx->active_session = UDS_SESSION_ID_DEFAULT;
        ctx->security_level = 0u; /* Reset security on default session */
        ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SESSION_CONTROL + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = UDS_SESSION_ID_DEFAULT;
        uds_send_response(ctx, 2u);
    } else if (sub == UDS_SESSION_ID_PROGRAMMING) { /* Programming Session */
        ctx->active_session = UDS_SESSION_ID_PROGRAMMING;
        ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SESSION_CONTROL + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = UDS_SESSION_ID_PROGRAMMING;
        /* Return P2/P2* timing params as per ISO (simplified placeholders) */
        ctx->config->tx_buffer[2] = 0x00u;
        ctx->config->tx_buffer[3] = 0x32u;
        ctx->config->tx_buffer[4] = 0x01u;
        ctx->config->tx_buffer[5] = 0xF4u;
        uds_send_response(ctx, 6u);
    } else if (sub == UDS_SESSION_ID_EXTENDED) { /* Extended Session */
        ctx->active_session = UDS_SESSION_ID_EXTENDED;
        ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SESSION_CONTROL + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = UDS_SESSION_ID_EXTENDED;
        ctx->config->tx_buffer[2] = 0x00u;
        ctx->config->tx_buffer[3] = 0x32u;
        ctx->config->tx_buffer[4] = 0x01u;
        ctx->config->tx_buffer[5] = 0xF4u;
        uds_send_response(ctx, 6u);
    } else {
        return uds_send_nrc(ctx, UDS_SID_SESSION_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
    
    /* NVM Persistence: Save State on Change */
    if (ctx->config->fn_nvm_save != NULL) {
        uint8_t state[2] = {ctx->active_session, ctx->security_level};
        ctx->config->fn_nvm_save(ctx, state, 2u);
    }

    return UDS_OK;
}

int uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    if (sub == 0x00u) {
        ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_TESTER_PRESENT + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = 0x00u;
        return uds_send_response(ctx, 2u);
    }
    return uds_send_nrc(ctx, UDS_SID_TESTER_PRESENT, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
}
