/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_flash.c
 * @brief Flash Engine Services: RoutineControl (0x31), RequestDownload (0x34),
 *        TransferData (0x36), RequestTransferExit (0x37)
 */

#include <string.h>
#include "uds_internal.h"

void uds_internal_handle_routine_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                         uds_result_t *out)
{
    if (len < 4u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t type = data[1];
    uint16_t id = (uint16_t) ((uint16_t) data[2] << 8u) | (uint16_t) data[3];

    if (ctx->config->fn_routine_control == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[4];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 4u);

    int written = ctx->config->fn_routine_control(ctx, type, id, &data[4], (uint16_t) (len - 4u),
                                                  out_payload, max_payload);

    if (written < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) written);
        return;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ROUTINE_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = type;
    ctx->config->tx_buffer[2] = data[2];
    ctx->config->tx_buffer[3] = data[3];
    uds_ok(out, (uint16_t) ((uint16_t) written + 4u));
}

void uds_internal_handle_request_download(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                          uds_result_t *out)
{
    /* ISO 14229-1: 0x34 [dataFormatIdentifier] [addressAndLengthFormatIdentifier] [address...]
     * [size...] */
    if (len < 4u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    /* uint8_t data_format = data[1]; */
    uint8_t addr_len_format = data[2];
    uint32_t addr, size;

    /* C-08: ALFID Validation (AddressAndLengthFormatIdentifier) */
    uint8_t addr_len = (uint8_t) (addr_len_format & 0x0Fu);
    uint8_t size_len = (uint8_t) ((addr_len_format >> 4u) & 0x0Fu);
    if (addr_len == 0u || size_len == 0u || addr_len > 4u || size_len > 4u) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    if (!uds_internal_parse_addr_len(&data[3], (uint16_t) (len - 3u), addr_len_format, &addr,
                                     &size)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_request_download == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    int res = ctx->config->fn_request_download(ctx, addr, size);
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    /* ISO 14229-1: Reset sequence counter and arm the transfer for new download */
    ctx->server.flash_sequence = 0u;
    ctx->server.transfer_active = true;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] =
        0x20u; /* Length format identifier (4 bytes for maxNumberOfBlockLength) */
    ctx->config->tx_buffer[2] = 0x00u;
    ctx->config->tx_buffer[3] = 0x00u;
    ctx->config->tx_buffer[4] = 0x04u;
    ctx->config->tx_buffer[5] = 0x00u;
    uds_ok(out, 6u);
}

void uds_internal_handle_transfer_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                       uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_transfer_data == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    /* ISO 14229-1: TransferData is only valid inside an active transfer started
     * by RequestDownload/RequestUpload; otherwise requestSequenceError (0x24). */
    if (!ctx->server.transfer_active) {
        uds_nrc(out, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        return;
    }

    uint8_t sequence = data[1];

    /* ISO 14229-1: Server shall track and verify the blockSequenceCounter. After
     * RequestDownload/Upload flash_sequence is 0, so the first expected block is
     * 0x01; thereafter it increments and wraps 0xFF -> 0x00. A mismatch is
     * wrongBlockSequenceCounter (0x73), distinct from "no transfer" (0x24). */
    uint8_t expected =
        (ctx->server.flash_sequence == 0xFFu) ? 0x00u : (uint8_t) (ctx->server.flash_sequence + 1u);
    if (sequence != expected) {
        /* Optional interoperability: accept last-block replay without re-processing data. */
        if (ctx->config->transfer_accept_last_block_replay &&
            (sequence == ctx->server.flash_sequence)) {
            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TRANSFER_DATA + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = sequence;
            uds_ok(out, 2u);
            return;
        }
        uds_nrc(out, UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER);
        return;
    }

    int res = ctx->config->fn_transfer_data(ctx, sequence, &data[2], (uint16_t) (len - 2u));
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    ctx->server.flash_sequence = sequence;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TRANSFER_DATA + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sequence;
    uds_ok(out, 2u);
}

void uds_internal_handle_request_transfer_exit(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                               uds_result_t *out)
{
    (void) data;
    (void) len;

    if (ctx->config->fn_transfer_exit == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    int res = ctx->config->fn_transfer_exit(ctx);
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    /* Transfer complete: further TransferData needs a new RequestDownload/Upload. */
    ctx->server.transfer_active = false;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_TRANSFER_EXIT + UDS_RESPONSE_OFFSET);
    uds_ok(out, 1u);
}

void uds_internal_handle_request_file_transfer(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                               uds_result_t *out)
{
    /* Request: 38 <mode> <pathLen hi> <pathLen lo> <path...> [params...] */
    uint8_t mode = data[1];

    /* modeOfOperation 1..5 (Add/Delete/Replace/Read/Resume). */
    if ((mode < 0x01u) || (mode > 0x05u)) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    uint16_t path_len = (uint16_t) (((uint16_t) data[2] << 8u) | (uint16_t) data[3]);
    if ((uint32_t) 4u + (uint32_t) path_len > (uint32_t) len) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_file_transfer == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    const uint8_t *path = &data[4];
    const uint8_t *params = &data[4u + path_len];
    uint16_t params_len = (uint16_t) (len - 4u - path_len);

    uint8_t *tx = ctx->config->tx_buffer;
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    int written = ctx->config->fn_file_transfer(ctx, mode, path, path_len, params, params_len,
                                                &tx[2], max_payload);
    if (written < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) written);
        return;
    }

    tx[0] = (uint8_t) (UDS_SID_REQUEST_FILE_TRANSFER + UDS_RESPONSE_OFFSET);
    tx[1] = mode;
    uds_ok(out, (uint16_t) ((uint16_t) written + 2u));
}

void uds_internal_handle_request_upload(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                        uds_result_t *out)
{
    /* ISO 14229-1: 0x35 [dataFormatIdentifier] [addressAndLengthFormatIdentifier] [address...]
     * [size...] */
    if (len < 4u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t addr_len_format = data[2];
    uint32_t addr, size;

    if (!uds_internal_parse_addr_len(&data[3], (uint16_t) (len - 3u), addr_len_format, &addr,
                                     &size)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_request_upload == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    int res = ctx->config->fn_request_upload(ctx, addr, size);
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    /* Reset sequence counter and arm the transfer for new upload */
    ctx->server.flash_sequence = 0u;
    ctx->server.transfer_active = true;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_REQUEST_UPLOAD + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = 0x20u;
    ctx->config->tx_buffer[2] = 0x00u;
    ctx->config->tx_buffer[3] = 0x00u;
    ctx->config->tx_buffer[4] = 0x04u;
    ctx->config->tx_buffer[5] = 0x00u;
    uds_ok(out, 6u);
}
