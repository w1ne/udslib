/**
 * @file uds_service_maintenance.c
 * @brief ECU Reset (0x11) & Communication Control (0x28)
 */

#include "uds_internal.h"

int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1];
    if (sub >= 0x01 && sub <= 0x03) {
        ctx->config->tx_buffer[0] = 0x51;
        ctx->config->tx_buffer[1] = sub;
        uds_send_response(ctx, 2);

        if (ctx->config->fn_reset) {
            ctx->config->fn_reset(ctx, sub);
        }
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x11, 0x12); /* Subfunction Not Supported */
}

int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t ctrl_type = data[1] & 0x7F;
    if (ctrl_type <= 0x03) {
        ctx->comm_state = ctrl_type;
        if (!(data[1] & 0x80)) { /* Suppress Pos Response */
            ctx->config->tx_buffer[0] = 0x68;
            ctx->config->tx_buffer[1] = data[1];
            uds_send_response(ctx, 2);
        }
        uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x28, 0x12);
}
