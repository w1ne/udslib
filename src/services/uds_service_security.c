/**
 * @file uds_service_security.c
 * @brief Security Access (0x27)
 */

#include "uds_internal.h"

int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1];
    if (sub == 0x01) { /* Request Seed */
        ctx->config->tx_buffer[0] = 0x67;
        ctx->config->tx_buffer[1] = 0x01;
        ctx->config->tx_buffer[2] = 0xDE;
        ctx->config->tx_buffer[3] = 0xAD;
        ctx->config->tx_buffer[4] = 0xBE;
        ctx->config->tx_buffer[5] = 0xEF;
        return uds_send_response(ctx, 6);
    } else if (sub == 0x02 && len >= 6) { /* Send Key */
        if (data[2] == 0xDF && data[3] == 0xAE && data[4] == 0xBF && data[5] == 0xF0) {
            ctx->security_level = 1;
            ctx->config->tx_buffer[0] = 0x67;
            ctx->config->tx_buffer[1] = 0x02;
            return uds_send_response(ctx, 2);
        } else {
            return uds_send_nrc(ctx, 0x27, 0x35);
        }
    }
    return -1;
}
