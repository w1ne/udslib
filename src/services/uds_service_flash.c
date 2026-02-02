/**
 * @file uds_service_flash.c
 * @brief Flash Engine Services: RoutineControl (0x31), RequestDownload (0x34),
 *        TransferData (0x36), RequestTransferExit (0x37)
 */

#include <string.h>
#include "uds_internal.h"

int uds_internal_handle_routine_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_ROUTINE_CONTROL,
                            UDS_NRC_INCORRECT_LENGTH); /* Incorrect Message Length */
    }

    uint8_t type = data[1];
    uint16_t id = (uint16_t) ((uint16_t) data[2] << 8u) | (uint16_t) data[3];

    if (ctx->config->fn_routine_control == NULL) {
        return uds_send_nrc(ctx, UDS_SID_ROUTINE_CONTROL,
                            UDS_NRC_CONDITIONS_NOT_CORRECT); /* Conditions Not Correct */
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[4];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 4u);

    int written = ctx->config->fn_routine_control(ctx, type, id, &data[4], (uint16_t) (len - 4u),
                                                  out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_ROUTINE_CONTROL, (uint8_t) - (int32_t) written);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ROUTINE_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = type;
    ctx->config->tx_buffer[2] = data[2];
    ctx->config->tx_buffer[3] = data[3];
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 4u));
}

int uds_internal_handle_request_download(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* ISO 14229-1: 0x34 [dataFormatIdentifier] [addressAndLengthFormatIdentifier] [address...]
     * [size...] */
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_INCORRECT_LENGTH);
    }

    /* uint8_t data_format = data[1]; */
    uint8_t addr_len_format = data[2];
    uint32_t addr, size;

    if (!uds_internal_parse_addr_len(&data[3], (uint16_t) (len - 3u), addr_len_format, &addr,
                                     &size)) {
        return uds_send_nrc(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_INCORRECT_LENGTH);
    }

    if (ctx->config->fn_request_download == NULL) {
        return uds_send_nrc(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    int res = ctx->config->fn_request_download(ctx, addr, size);
    if (res < 0) {
        return uds_send_nrc(ctx, UDS_SID_REQUEST_DOWNLOAD, (uint8_t) - (int32_t) res);
    }

    /* ISO 14229-1: Reset sequence counter for new transfer */
    ctx->flash_sequence = 0u;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] =
        0x20u; /* Length format identifier (4 bytes for maxNumberOfBlockLength) */
    ctx->config->tx_buffer[2] = 0x00u; /* Placeholder max block length */
    ctx->config->tx_buffer[3] = 0x00u;
    ctx->config->tx_buffer[4] = 0x04u;
    ctx->config->tx_buffer[5] = 0x00u;
    return uds_send_response(ctx, 6u);
}

int uds_internal_handle_transfer_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_INCORRECT_LENGTH);
    }

    if (ctx->config->fn_transfer_data == NULL) {
        return uds_send_nrc(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    uint8_t sequence = data[1];

    /* ISO 14229-1: Server shall track and verify sequence counter */
    if (ctx->flash_sequence == 0u) {
        /* First block must be 0x01 */
        if (sequence != 0x01u) {
            return uds_send_nrc(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        }
    }
    else {
        uint8_t expected =
            (ctx->flash_sequence == 0xFFu) ? 0x00u : (uint8_t) (ctx->flash_sequence + 1u);
        if (sequence != expected) {
            return uds_send_nrc(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        }
    }

    int res = ctx->config->fn_transfer_data(ctx, sequence, &data[2], (uint16_t) (len - 2u));
    if (res < 0) {
        return uds_send_nrc(ctx, UDS_SID_TRANSFER_DATA, (uint8_t) - (int32_t) res);
    }

    ctx->flash_sequence = sequence;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TRANSFER_DATA + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sequence;
    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_request_transfer_exit(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) data;
    (void) len;

    if (ctx->config->fn_transfer_exit == NULL) {
        return uds_send_nrc(ctx, UDS_SID_TRANSFER_EXIT, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    int res = ctx->config->fn_transfer_exit(ctx);
    if (res < 0) {
        return uds_send_nrc(ctx, UDS_SID_TRANSFER_EXIT, (uint8_t) - (int32_t) res);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TRANSFER_EXIT + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, 1u);
}
