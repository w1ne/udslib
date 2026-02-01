/**
 * @file uds_service_security.c
 * @brief Security Access (0x27)
 */

#include "uds_internal.h"

int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1] & 0x7F;
    if (sub % 2 != 0) { /* Request Seed (Odd subfunctions: 0x01, 0x03...) */
        if (!ctx->config->fn_security_seed) {
            return uds_send_nrc(ctx, 0x27, 0x22); /* Conditions Not Correct */
        }
        
        ctx->config->tx_buffer[0] = 0x67;
        ctx->config->tx_buffer[1] = data[1];
        
        int seed_len = ctx->config->fn_security_seed(ctx, (sub + 1) / 2, &ctx->config->tx_buffer[2], ctx->config->tx_buffer_size - 2);
        if (seed_len < 0) {
            return uds_send_nrc(ctx, 0x27, (uint8_t)(-seed_len));
        }
        return uds_send_response(ctx, seed_len + 2);
    } else { /* Send Key (Even subfunctions: 0x02, 0x04...) */
        if (!ctx->config->fn_security_key) {
            return uds_send_nrc(ctx, 0x27, 0x22);
        }

        /* Standard assumes key is passed in the request after SID and subfn */
        int res = ctx->config->fn_security_key(ctx, sub / 2, NULL, &data[2], len - 2);
        if (res == 0) {
            ctx->security_level = sub / 2;
            
            /* NVM Persistence: Save State */
            if (ctx->config->fn_nvm_save) {
                uint8_t state[2] = {ctx->active_session, ctx->security_level};
                ctx->config->fn_nvm_save(ctx, state, 2);
            }

            ctx->config->tx_buffer[0] = 0x67;
            ctx->config->tx_buffer[1] = data[1];
            return uds_send_response(ctx, 2);
        } else {
            return uds_send_nrc(ctx, 0x27, (uint8_t)(-res));
        }
    }
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
