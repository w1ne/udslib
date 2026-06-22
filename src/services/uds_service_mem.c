/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_mem.c
 * @brief Memory Services Implementation (0x23, 0x3D)
 */

#include "uds_internal.h"
#include <string.h>

void uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                             uds_result_t *out)
{
    if (len < 3u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t format = data[1];
    uint32_t addr, size;

    /* C-09: ALFID Validation. Both nibbles must be non-zero (1-4). */
    uint8_t addr_len = (uint8_t) (format & 0x0Fu);
    uint8_t size_len = (uint8_t) ((format >> 4u) & 0x0Fu);
    if (addr_len == 0u || size_len == 0u || addr_len > 4u || size_len > 4u) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    if (!uds_internal_parse_addr_len(&data[2], (uint16_t) (len - 2u), format, &addr, &size)) {
        /* This path handles case where provided data is shorter than ALFID specified */
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_mem_read == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (size > (uint32_t) (ctx->config->tx_buffer_size - 1u)) {
        uds_nrc(out, UDS_NRC_RESPONSE_TOO_LONG);
        return;
    }

    int res = ctx->config->fn_mem_read(ctx, addr, size, &ctx->config->tx_buffer[1]);
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_MEM_BY_ADDR + UDS_RESPONSE_OFFSET);
    uds_ok(out, (uint16_t) (size + 1u));
}

void uds_internal_handle_write_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                              uds_result_t *out)
{
    if (len < 3u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t format = data[1];
    uint32_t addr, size;
    uint8_t addr_len = (uint8_t) (format & 0x0Fu);
    uint8_t size_len = (uint8_t) ((format >> 4u) & 0x0Fu);

    /* C-09: ALFID Validation */
    if (addr_len == 0u || size_len == 0u || addr_len > 4u || size_len > 4u) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    uint16_t consumed = (uint16_t) (2u + (uint16_t) addr_len + (uint16_t) size_len);

    if (!uds_internal_parse_addr_len(&data[2], (uint16_t) (len - 2u), format, &addr, &size)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (len != (uint16_t) (consumed + (uint16_t) size)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_mem_write == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (ctx->config->fn_is_safe &&
        !ctx->config->fn_is_safe(ctx, UDS_SID_WRITE_MEM_BY_ADDR, data, len)) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    int res = ctx->config->fn_mem_write(ctx, addr, size, &data[consumed]);
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_WRITE_MEM_BY_ADDR + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = format;

    /* C-20: ISO 14229-1: Server shall echo the address and size if successfully written */
    for (uint16_t i = 0u; i < (uint16_t) ((uint16_t) addr_len + (uint16_t) size_len); i++) {
        ctx->config->tx_buffer[2u + i] = data[2u + i];
    }

    uds_ok(out, (uint16_t) (2u + (uint16_t) addr_len + (uint16_t) size_len));
}
