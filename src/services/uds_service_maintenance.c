/**
 * @file uds_service_maintenance.c
 * @brief Maintenance Services: ECU Reset (0x11), Comm Control (0x28), and DTC Management (0x14,
 * 0x19, 0x85)
 */

#include "uds_internal.h"
#include <string.h>

int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_ECU_RESET, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    /* ISO 14229-1:2013 Table 21: 0x01-0x05 */
    if ((sub < 0x01u) || (sub > 0x05u)) {
        return uds_send_nrc(ctx, UDS_SID_ECU_RESET, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Process Reset */
    if (ctx->config->fn_reset) {
        ctx->config->fn_reset(ctx, sub);
    }

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ECU_RESET + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* C-11: Minimum length is 3 bytes (SI+Control+Comm) */
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t ctrl_type = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);
    uint8_t comm_type = data[2];

    if (ctrl_type > 0x05u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    /* ISO 14229-1: communicationType lower nibble must be 1, 2, or 3 */
    uint8_t type_nibble = (uint8_t) (comm_type & 0x0Fu);
    if ((type_nibble == 0u) || (type_nibble > 3u)) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Check App Callback */
    if (ctx->config->fn_comm_control) {
        int ret = ctx->config->fn_comm_control(ctx, ctrl_type, comm_type);
        if (ret != UDS_OK) {
            return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, (uint8_t) - (int32_t) ret);
        }
    }

    ctx->comm_state = ctrl_type;
    uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_COMM_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = ctrl_type;

    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC,
                            UDS_NRC_INCORRECT_LENGTH); /* Incorrect Msg Length */
    }

    if (!ctx->config->fn_dtc_clear) {
        return uds_send_nrc(
            ctx, UDS_SID_CLEAR_DTC,
            UDS_NRC_CONDITIONS_NOT_CORRECT); /* Conditions Not Correct (No clear hook) */
    }

    uint32_t group = (uint32_t) ((uint32_t) data[1] << 16u) |
                     (uint32_t) ((uint32_t) data[2] << 8u) | (uint32_t) data[3];
    int res = ctx->config->fn_dtc_clear(ctx, group);

    if (res != UDS_OK) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC, (uint8_t) - (int32_t) res);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CLEAR_DTC + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, 1u);
}

int uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    /* C-10: DTC Status Mask Requirement */
    bool req_mask = (sub == 0x01u || sub == 0x02u || sub == 0x07u || sub == 0x08u || sub == 0x0Fu ||
                     sub == 0x11u || sub == 0x12u || sub == 0x13u);

    if (req_mask && len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    if (!ctx->config->fn_dtc_read) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    /* Pass subfunction and optional mask (data[2]) to the application */
    int written = ctx->config->fn_dtc_read(ctx, sub, out_payload, max_payload);
    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) written);
    }

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 2u));
}

int uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    if ((sub != 0x01u) && (sub != 0x02u)) { /* ON / OFF */
        return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Process DTC Setting Control (usually global flag in ctx or config) */
    /* ... application should probably handle this via a hook if needed ... */

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CONTROL_DTC_SETTING + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, 2u);
}
