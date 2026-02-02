/**
 * @file uds_service_security.c
 * @brief Security Access (0x27) & Authentication (0x29)
 */

#include "uds_internal.h"

int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_INCORRECT_LENGTH);
    }

    uint32_t now = ctx->config->get_time_ms();

    /* C-14: Security Delay Timer Check */
    if (ctx->security_delay_end != 0u) {
        if (now < ctx->security_delay_end) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_REQUIRED_TIME_DELAY);
        }
        /* Delay expired, reset counter if we want, but ISO says just allow next attempt */
        ctx->security_delay_end = 0u;
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);

    if ((sub % 2u) != 0u) { /* Request Seed (Odd subfunctions: 0x01, 0x03...) */
        if (ctx->config->fn_security_seed == NULL) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_CONDITIONS_NOT_CORRECT);
        }

        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
        ctx->config->tx_buffer[1] = sub_raw;

        int seed_len = ctx->config->fn_security_seed(ctx, (uint8_t) (((uint16_t) sub + 1u) / 2u),
                                                     &ctx->config->tx_buffer[2],
                                                     (uint16_t) (ctx->config->tx_buffer_size - 2u));
        if (seed_len < 0) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, (uint8_t) - (int32_t) seed_len);
        }
        return uds_send_response(ctx, (uint16_t) ((uint16_t) seed_len + 2u));
    }
    else { /* Send Key (Even subfunctions: 0x02, 0x04...) */
        if (ctx->config->fn_security_key == NULL) {
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_CONDITIONS_NOT_CORRECT);
        }

        /* Standard assumes key is passed in the request after SID and subfn */
        int res = ctx->config->fn_security_key(ctx, (uint8_t) (sub / 2u), NULL, &data[2],
                                               (uint16_t) (len - 2u));
        if (res == 0) {
            /* Success! Reset attempts */
            ctx->security_attempts = 0u;
            ctx->security_level = (uint8_t) (sub / 2u);

            /* NVM Persistence: Save State */
            if (ctx->config->fn_nvm_save) {
                uint8_t state[2] = {ctx->active_session, ctx->security_level};
                ctx->config->fn_nvm_save(ctx, state, 2u);
            }

            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_SECURITY_ACCESS + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sub_raw;
            return uds_send_response(ctx, 2u);
        }
        else {
            /* C-15: Attempt Management */
            ctx->security_attempts++;
            uint8_t max_att =
                ctx->config->security_max_attempts ? ctx->config->security_max_attempts : 3u;

            if (ctx->security_attempts >= max_att) {
                uint32_t delay =
                    ctx->config->security_delay_ms ? ctx->config->security_delay_ms : 10000u;
                ctx->security_delay_end = now + delay;
                return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, UDS_NRC_EXCEEDED_ATTEMPTS);
            }

            /* Return NRC provided by key handler, or 0x35 (InvalidKey) */
            uint8_t nrc = (res < 0) ? (uint8_t) - (int32_t) res : UDS_NRC_INVALID_KEY;
            return uds_send_nrc(ctx, UDS_SID_SECURITY_ACCESS, nrc);
        }
    }
}

int uds_internal_handle_authentication(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);

    if (ctx->config->fn_auth == NULL) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    /* Payload begins at data[2]. Output payload at tx_buffer[2] */
    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    int written =
        ctx->config->fn_auth(ctx, sub, &data[2], (uint16_t) (len - 2u), out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_AUTHENTICATION, (uint8_t) - (int32_t) written);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_AUTHENTICATION + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 2u));
}
