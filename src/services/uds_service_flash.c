/**
 * @file uds_service_flash.c
 * @brief Flash Engine Services: RoutineControl (0x31), RequestDownload (0x34), 
 *        TransferData (0x36), RequestTransferExit (0x37)
 */

#include <string.h>
#include "uds_internal.h"

int uds_internal_handle_routine_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4) {
        return uds_send_nrc(ctx, 0x31, 0x13); /* Incorrect Message Length */
    }

    uint8_t type = data[1];
    uint16_t id = (uint16_t)((uint16_t)data[2] << 8) | (uint16_t)data[3];

    if (!ctx->config->fn_routine_control) {
        return uds_send_nrc(ctx, 0x31, 0x22); /* Conditions Not Correct */
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[4];
    uint16_t max_payload = ctx->config->tx_buffer_size - 4;

    int written = ctx->config->fn_routine_control(ctx, type, id, &data[4], len - 4, out_payload, max_payload);

    if (written < 0) {
        return uds_send_nrc(ctx, 0x31, (uint8_t)(-written));
    }

    ctx->config->tx_buffer[0] = 0x71;
    ctx->config->tx_buffer[1] = type;
    ctx->config->tx_buffer[2] = data[2];
    ctx->config->tx_buffer[3] = data[3];
    return uds_send_response(ctx, written + 4);
}

int uds_internal_handle_request_download(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* ISO 14229-1: 0x34 [dataFormatIdentifier] [addressAndLengthFormatIdentifier] [address...] [size...] */
    if (len < 4) {
        return uds_send_nrc(ctx, 0x34, 0x13);
    }

    uint8_t data_format = data[1];
    uint8_t addr_len_format = data[2];
    uint32_t addr, size;

    if (!uds_internal_parse_addr_len(&data[3], len - 3, addr_len_format, &addr, &size)) {
        return uds_send_nrc(ctx, 0x34, 0x13);
    }

    if (!ctx->config->fn_request_download) {
        return uds_send_nrc(ctx, 0x34, 0x22);
    }

    int res = ctx->config->fn_request_download(ctx, addr, size);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x34, (uint8_t)(-res));
    }

    /* ISO 14229-1: Reset sequence counter for new transfer */
    ctx->flash_sequence = 0;

    ctx->config->tx_buffer[0] = 0x74;
    ctx->config->tx_buffer[1] = 0x20; /* Length format identifier (4 bytes for maxNumberOfBlockLength) */
    ctx->config->tx_buffer[2] = 0x00; /* Placeholder max block length */
    ctx->config->tx_buffer[3] = 0x00;
    ctx->config->tx_buffer[4] = 0x04;
    ctx->config->tx_buffer[5] = 0x00;
    return uds_send_response(ctx, 6);
}

int uds_internal_handle_transfer_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return uds_send_nrc(ctx, 0x36, 0x13);
    }

    if (!ctx->config->fn_transfer_data) {
        return uds_send_nrc(ctx, 0x36, 0x22);
    }

    uint8_t sequence = data[1];

    /* ISO 14229-1: Server shall track and verify sequence counter */
    if (ctx->flash_sequence == 0) {
        /* First block must be 0x01 */
        if (sequence != 0x01) {
            return uds_send_nrc(ctx, 0x36, 0x24); /* Request Sequence Error */
        }
    } else {
        uint8_t expected = (ctx->flash_sequence == 0xFF) ? 0x00 : (ctx->flash_sequence + 1);
        if (sequence != expected) {
            return uds_send_nrc(ctx, 0x36, 0x24);
        }
    }

    int res = ctx->config->fn_transfer_data(ctx, sequence, &data[2], len - 2);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x36, (uint8_t)(-res));
    }

    ctx->flash_sequence = sequence;

    ctx->config->tx_buffer[0] = 0x76;
    ctx->config->tx_buffer[1] = sequence;
    return uds_send_response(ctx, 2);
}

int uds_internal_handle_request_transfer_exit(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;

    if (!ctx->config->fn_transfer_exit) {
        return uds_send_nrc(ctx, 0x37, 0x22);
    }

    int res = ctx->config->fn_transfer_exit(ctx);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x37, (uint8_t)(-res));
    }

    ctx->config->tx_buffer[0] = 0x77;
    return uds_send_response(ctx, 1);
}
