/**
 * @file uds_service_security.c
 * @brief Security Access (0x27) & Authentication (0x29)
 */

#include "uds_internal.h"

int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* Check Delay Timer */
    if (ctx->security_delay_active) {
        uint32_t now = ctx->config->get_time_ms();
        if ((now - ctx->security_delay_start) < 10000u) { /* 10s Delay */
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_REQUIRED_TIME_DELAY);
        }
        ctx->security_delay_active = false;
        ctx->security_attempt_count = 0u;
        uds_internal_log(ctx, UDS_LOG_INFO, "Security Delay Expired");
    }

    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);

    if ((sub % 2u) != 0u) { /* Request Seed (Odd subfunctions: 0x01, 0x03...) */
        if (ctx->config->fn_security_seed == NULL) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_CONDITIONS_NOT_CORRECT);
        }
        
        ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = data[1];
        
        int seed_len = ctx->config->fn_security_seed(ctx, (uint8_t)(((uint16_t)sub + 1u) / 2u), &ctx->config->tx_buffer[2], (uint16_t)(ctx->config->tx_buffer_size - 2u));
        if (seed_len < 0) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, (uint8_t)-(int32_t)seed_len);
        }
        return uds_send_response(ctx, (uint16_t)((uint16_t)seed_len + 2u));
    } else { /* Send Key (Even subfunctions: 0x02, 0x04...) */
        if (ctx->config->fn_security_key == NULL) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_CONDITIONS_NOT_CORRECT);
        }

        /* Standard assumes key is passed in the request after SID and subfn */
        int res = ctx->config->fn_security_key(ctx, (uint8_t)(sub / 2u), NULL, &data[2], (uint16_t)(len - 2u));
        if (res == 0) {
            ctx->security_level = (uint8_t)(sub / 2u);
            ctx->security_attempt_count = 0u;
            
            /* NVM Persistence: Save State */
            if (ctx->config->fn_nvm_save != NULL) {
                uint8_t state[2] = {ctx->active_session, ctx->security_level};
                ctx->config->fn_nvm_save(ctx, state, 2u);
            }

            ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = data[1];
            return uds_send_response(ctx, 2u);
        } else {
            /* Failed Attempt Logic */
            ctx->security_attempt_count++;
            if (ctx->security_attempt_count >= 3u) {
                ctx->security_delay_active = true;
                ctx->security_delay_start = ctx->config->get_time_ms();
                uds_internal_log(ctx, UDS_LOG_ERROR, "Security Locked Out (3 Failed Attempts)");
            }
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_INVALID_KEY);
        }
    }
}

int uds_internal_handle_authentication(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub = (uint8_t)(data[1] & 0x7Fu);

    if (ctx->config->fn_auth == NULL) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    /* Payload begins at data[2]. Output payload at tx_buffer[2] */
    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t)(ctx->config->tx_buffer_size - 2u);

    int written = ctx->config->fn_auth(ctx, sub, &data[2], (uint16_t)(len - 2u), out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, (uint8_t)-(int32_t)written);
    }

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_AUTHENTICATION + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];
    return uds_send_response(ctx, (uint16_t)((uint16_t)written + 2u));
}
