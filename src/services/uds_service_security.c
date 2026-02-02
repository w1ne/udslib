/**
 * @file uds_service_security.c
 * @brief Security Access (0x27)
 */

#include "uds_internal.h"

int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* C-14: Check Delay Timer */
    if (ctx->security_delay_active) {
        uint32_t now = ctx->config->get_time_ms();
        if ((now - ctx->security_delay_start) < 10000) { /* 10s Delay */
            return uds_send_nrc(ctx, 0x27, 0x37); /* Required Time Delay Not Expired */
        }
        ctx->security_delay_active = false;
        ctx->security_attempt_count = 0;
        uds_internal_log(ctx, UDS_LOG_INFO, "Security Delay Expired");
    }

    uint8_t sub = data[1];
    
    if (sub == 0x01) { /* Request Seed */
        ctx->config->tx_buffer[0] = 0x67;
        ctx->config->tx_buffer[1] = 0x01;
        ctx->config->tx_buffer[2] = 0xDE;
        ctx->config->tx_buffer[3] = 0xAD;
        ctx->config->tx_buffer[4] = 0xBE;
        ctx->config->tx_buffer[5] = 0xEF;
        return uds_send_response(ctx, 6);
    } 
    else if (sub == 0x02) { /* Send Key */
        if (len < 6) {
            return uds_send_nrc(ctx, 0x27, 0x13);
        }

        if (data[2] == 0xDF && data[3] == 0xAE && data[4] == 0xBF && data[5] == 0xF0) {
            ctx->security_level = 1;
            ctx->security_attempt_count = 0;
            
            /* NVM Persistence: Save State */
            if (ctx->config->fn_nvm_save) {
                uint8_t state[2] = {ctx->active_session, ctx->security_level};
                ctx->config->fn_nvm_save(ctx, state, 2);
            }

            ctx->config->tx_buffer[0] = 0x67;
            ctx->config->tx_buffer[1] = 0x02;
            return uds_send_response(ctx, 2);
        } else {
            /* C-14: Failed Attempt Logic */
            ctx->security_attempt_count++;
            if (ctx->security_attempt_count >= 3) {
                ctx->security_delay_active = true;
                ctx->security_delay_start = ctx->config->get_time_ms();
                uds_internal_log(ctx, UDS_LOG_ERROR, "Security Locked Out (3 Failed Attempts)");
            }
            return uds_send_nrc(ctx, 0x27, 0x35); /* Invalid Key */
        }
    }
    
    /* C-04: Invalid Subfunction */
    return uds_send_nrc(ctx, 0x27, 0x12);
}

int uds_internal_handle_authentication(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return uds_send_nrc(ctx, 0x29, 0x13); /* Incorrect Msg Length */
    }

    uint8_t sub = data[1] & 0x7F;

    if (!ctx->config->fn_auth) {
        return uds_send_nrc(ctx, 0x29, 0x22); /* Conditions Not Correct */
    }

    /* Payload begins at data[2]. Output payload at tx_buffer[2] */
    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = ctx->config->tx_buffer_size - 2;

    int written = ctx->config->fn_auth(ctx, sub, &data[2], len - 2, out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, 0x29, (uint8_t)(-written));
    }

    ctx->config->tx_buffer[0] = 0x69;
    ctx->config->tx_buffer[1] = data[1];
    return uds_send_response(ctx, written + 2);
}
