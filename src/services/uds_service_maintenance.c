/**
 * @file uds_service_maintenance.c
 * @brief Maintenance Services: ECU Reset (0x11), Comm Control (0x28), and DTC Management (0x14, 0x19, 0x85)
 */

#include "uds_internal.h"
#include <string.h>

int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1] & 0x7F; /* C-03: Mask out suppress bit */
    
    if (sub >= 0x01 && sub <= 0x03) {
        if (!(data[1] & 0x80)) { /* Check Suppress Bit */
            ctx->config->tx_buffer[0] = 0x51;
            ctx->config->tx_buffer[1] = sub;
            uds_send_response(ctx, 2);
        }

        if (ctx->config->fn_reset) {
            ctx->config->fn_reset(ctx, sub);
        }
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x11, 0x12);
}

int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3) { /* C-11: Min Length 3 */
        return uds_send_nrc(ctx, 0x28, 0x13);
    }

    uint8_t ctrl_type = data[1] & 0x7F;
    uint8_t comm_type = data[2];

    if (ctrl_type <= 0x03) {
        if (ctx->config->fn_comm_control) {
            int ret = ctx->config->fn_comm_control(ctx, ctrl_type, comm_type);
            if (ret != UDS_OK) {
                return uds_send_nrc(ctx, 0x28, (uint8_t)(-ret)); 
            }
        }

        ctx->comm_state = ctrl_type;
        if (!(data[1] & 0x80)) {
            ctx->config->tx_buffer[0] = 0x68;
            ctx->config->tx_buffer[1] = ctrl_type;
            return uds_send_response(ctx, 2);
        }
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x28, 0x12);
}

int uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4) {
        return uds_send_nrc(ctx, 0x14, 0x13);
    }

    uint32_t group = (data[1] << 16) | (data[2] << 8) | data[3];
    
    if (!ctx->config->fn_dtc_clear) {
        return uds_send_nrc(ctx, 0x14, 0x22);
    }

    int res = ctx->config->fn_dtc_clear(ctx, group);
    if (res == UDS_OK) {
        ctx->config->tx_buffer[0] = 0x54;
        return uds_send_response(ctx, 1);
    }
    return uds_send_nrc(ctx, 0x14, (uint8_t)(-res));
}

int uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1] & 0x7F;
    uint8_t mask = 0;

    /* C-10: Extract Status Mask if applicable (0x01, 0x02, 0x11, 0x12, etc) */
    if (len >= 3) {
        mask = data[2];
    } else if (sub == 0x01 || sub == 0x02) {
        return uds_send_nrc(ctx, 0x19, 0x13); /* Mask required */
    }

    if (!ctx->config->fn_dtc_read) {
        return uds_send_nrc(ctx, 0x19, 0x22);
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = ctx->config->tx_buffer_size - 2;

    int written = ctx->config->fn_dtc_read(ctx, sub, mask, out_payload, max_payload);
    if (written < 0) {
        return uds_send_nrc(ctx, 0x19, (uint8_t)(-written));
    }

    ctx->config->tx_buffer[0] = 0x59;
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, written + 2);
}

int uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = data[1] & 0x7F;
    uint32_t group = 0xFFFFFF; /* Default: All */
    
    /* C-16: Parse GroupOfDTC if present */
    if (len >= 5) {
        group = (data[2] << 16) | (data[3] << 8) | data[4];
    }

    if (sub == 0x01 || sub == 0x02) { /* ON / OFF */
        if (ctx->config->fn_control_dtc) {
            int ret = ctx->config->fn_control_dtc(ctx, sub, group);
            if (ret != UDS_OK) {
                return uds_send_nrc(ctx, 0x85, (uint8_t)(-ret));
            }
        }

        if (!(data[1] & 0x80)) {
            ctx->config->tx_buffer[0] = 0xC5;
            ctx->config->tx_buffer[1] = sub;
            uds_send_response(ctx, 2);
        }
        return UDS_OK;
    }
    return uds_send_nrc(ctx, 0x85, 0x12);
}

