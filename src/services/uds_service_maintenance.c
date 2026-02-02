/**
 * @file uds_service_maintenance.c
 * @brief Maintenance Services: ECU Reset (0x11), Comm Control (0x28), and DTC Management (0x14, 0x19, 0x85)
 */

#include "uds_internal.h"
#include <string.h>

int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);

    if ((sub < 0x01u) || (sub > 0x03u)) {
        return uds_send_nrc(ctx, UDS_SID_ECU_RESET, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_ECU_RESET + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    uds_send_response(ctx, 2u);

    if (ctx->config->fn_reset != NULL) {
        ctx->config->fn_reset(ctx, sub);
    }
    return UDS_OK;
}

int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t ctrl_type = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    uint8_t comm_type = data[2];

    if (ctrl_type > 0x03u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    /* ISO 14229-1: communicationType lower nibble must be 1, 2, or 3 */
    uint8_t type_nibble = (uint8_t)(comm_type & 0x03u);
    if ((type_nibble == 0u) || (type_nibble > 3u)) {
         return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* Check App Callback */
    if (ctx->config->fn_comm_control != NULL) {
        int ret = ctx->config->fn_comm_control(ctx, ctrl_type, comm_type);
        if (ret != UDS_OK) {
            return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, (uint8_t)-(int32_t)ret); 
        }
    }

    ctx->comm_state = ctrl_type;
    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_COMM_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];

    uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");
    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC, UDS_NRC_INCORRECT_LENGTH);
    }

    if (!ctx->config->fn_dtc_clear) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    uint32_t group = (uint32_t)((uint32_t)data[1] << 16u) | (uint32_t)((uint32_t)data[2] << 8u) | (uint32_t)data[3];
    int res = ctx->config->fn_dtc_clear(ctx, group);

    if (res != UDS_OK) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC, (uint8_t)-(int32_t)res);
    }

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_CLEAR_DTC + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, 1u);
}

int uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    uint8_t mask = 0u;

    if (len >= 3u) {
        mask = data[2];
    } else if ((sub == 0x01u) || (sub == 0x02u)) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    if (!ctx->config->fn_dtc_read) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t)(ctx->config->tx_buffer_size - 2u);

    int written = ctx->config->fn_dtc_read(ctx, sub, mask, out_payload, max_payload);
    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t)-(int32_t)written);
    }

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];
    return uds_send_response(ctx, (uint16_t)((uint16_t)written + 2u));
}

int uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t)(data[1] & UDS_MASK_SUBFUNCTION);
    uint32_t group = 0xFFFFFFu; /* Default: All */
    
    if (len >= 5u) {
        group = (uint32_t)((uint32_t)data[2] << 16u) | (uint32_t)((uint32_t)data[3] << 8u) | (uint32_t)data[4];
    }

    if ((sub != 0x01u) && (sub != 0x02u)) {
        return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    if (ctx->config->fn_control_dtc != NULL) {
        int ret = ctx->config->fn_control_dtc(ctx, sub, group);
        if (ret != UDS_OK) {
            return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, (uint8_t)-(int32_t)ret);
        }
    }

    ctx->config->tx_buffer[0] = (uint8_t)(UDS_SID_CONTROL_DTC_SETTING + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];
    return uds_send_response(ctx, 2u);
}

