/**
 * @file uds_service_session.c
 * @brief Diagnostic Session Control (0x10) & Tester Present (0x3E)
 */

#include "uds_internal.h"

int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    
    if (sub != UDS_SESSION_ID_DEFAULT && sub != UDS_SESSION_ID_PROGRAMMING && sub != UDS_SESSION_ID_EXTENDED) {
        return uds_send_nrc(ctx, UDS_SID_SESSION_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    /* Security Reset on Transition */
    if (ctx->active_session != sub) {
        ctx->security_level = 0u;
        ctx->active_session = sub;
        uds_internal_log(ctx, UDS_LOG_INFO, "Session Changed. Security Reset.");
    }

    uint16_t p2 = ctx->config->p2_ms;
    uint16_t p2_star_10ms = (uint16_t)(ctx->config->p2_star_ms / 10u);

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SESSION_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    ctx->config->tx_buffer[2] = (uint8_t)((p2 >> 8u) & 0xFFu);
    ctx->config->tx_buffer[3] = (uint8_t)(p2 & 0xFFu);
    ctx->config->tx_buffer[4] = (uint8_t)((p2_star_10ms >> 8u) & 0xFFu);
    ctx->config->tx_buffer[5] = (uint8_t)(p2_star_10ms & 0xFFu);

    uds_send_response(ctx, 6u);

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

