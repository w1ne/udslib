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
    uint16_t id = (data[2] << 8) | data[3];

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
    /* Basic 0x34 implementation: Expecting 1 byte format, 4 byte addr, 4 byte size = 10 bytes */
    if (len < 10) {
        return uds_send_nrc(ctx, 0x34, 0x13);
    }

    if (!ctx->config->fn_request_download) {
        return uds_send_nrc(ctx, 0x34, 0x22);
    }

    uint32_t addr = (data[3] << 24) | (data[4] << 16) | (data[5] << 8) | data[6];
    uint32_t size = (data[7] << 24) | (data[8] << 16) | (data[9] << 8) | data[10];

    int res = ctx->config->fn_request_download(ctx, addr, size);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x34, (uint8_t)(-res));
    }

    ctx->config->tx_buffer[0] = 0x74;
    ctx->config->tx_buffer[1] = 0x20; /* Length format identifier (maxNumberOfBlockLength) */
    ctx->config->tx_buffer[2] = 0x04; /* Placeholder max block length */
    ctx->config->tx_buffer[3] = 0x00;
    return uds_send_response(ctx, 4);
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
    int res = ctx->config->fn_transfer_data(ctx, sequence, &data[2], len - 2);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x36, (uint8_t)(-res));
    }

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
