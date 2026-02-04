/**
 * @file uds_service_io.c
 * @brief Input Output Control By Identifier (0x2F)
 */

#include <string.h>
#include "uds_internal.h"

int uds_internal_handle_io_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* ISO 14229-1: 0x2F [DID high] [DID low] [controlOptionRecord...] [controlEnableMaskRecord...] */
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_INCORRECT_LENGTH);
    }

    uint16_t id = (uint16_t) ((uint16_t) data[1] << 8u) | (uint16_t) data[2];
    uint8_t ctrl_type = data[3];

    if (ctx->config->fn_io_control == NULL) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_SERVICE_NOT_SUPPORTED);
    }

    /* Check if DID exists in table (optional, but good for validation) */
    const uds_did_entry_t *entry = uds_internal_find_did(ctx, id);
    if (entry == NULL) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* Security/Session check for the DID */
    if (entry->session_mask != 0u && !((1u << (ctx->active_session - 1u)) & entry->session_mask)) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_SERVICE_NOT_SUPP_IN_SESS);
    }
    if (entry->security_mask != 0u && !((1u << ctx->security_level) & entry->security_mask)) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_SECURITY_ACCESS_DENIED);
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[3];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 3u);

    /* controlOptionRecord starts at data[4] */
    int written = ctx->config->fn_io_control(ctx, id, ctrl_type, &data[4], (uint16_t) (len - 4u),
                                             out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_IO_CONTROL_BY_ID, (uint8_t) - (int32_t) written);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_IO_CONTROL_BY_ID + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = data[1];
    ctx->config->tx_buffer[2] = data[2];
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 3u));
}
