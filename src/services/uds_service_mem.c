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

int uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t format = data[1];
    uint32_t addr, size;

    /* C-09: ALFID Validation. Both nibbles must be non-zero (1-4). */
    uint8_t addr_len = (uint8_t) (format & 0x0Fu);
    uint8_t size_len = (uint8_t) ((format >> 4u) & 0x0Fu);
    if (addr_len == 0u || size_len == 0u || addr_len > 4u || size_len > 4u) {
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    if (!uds_internal_parse_addr_len(&data[2], (uint16_t) (len - 2u), format, &addr, &size)) {
        /* This path handles case where provided data is shorter than ALFID specified */
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, UDS_NRC_INCORRECT_LENGTH);
    }

    if (ctx->config->fn_mem_read == NULL) {
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    if (size > (uint32_t) (ctx->config->tx_buffer_size - 1u)) {
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, UDS_NRC_RESPONSE_TOO_LONG);
    }

    int res = ctx->config->fn_mem_read(ctx, addr, size, &ctx->config->tx_buffer[1]);
    if (res < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_MEM_BY_ADDR, (uint8_t) - (int32_t) res);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_MEM_BY_ADDR + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, (uint16_t) (size + 1u));
}

int uds_internal_handle_write_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t format = data[1];
    uint32_t addr, size;
    uint8_t addr_len = (uint8_t) (format & 0x0Fu);
    uint8_t size_len = (uint8_t) ((format >> 4u) & 0x0Fu);

    /* C-09: ALFID Validation */
    if (addr_len == 0u || size_len == 0u || addr_len > 4u || size_len > 4u) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    uint16_t consumed = (uint16_t) (2u + (uint16_t) addr_len + (uint16_t) size_len);

    if (!uds_internal_parse_addr_len(&data[2], (uint16_t) (len - 2u), format, &addr, &size)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_INCORRECT_LENGTH);
    }

    if (len != (uint16_t) (consumed + (uint16_t) size)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_INCORRECT_LENGTH);
    }

    if (ctx->config->fn_mem_write == NULL) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    if (ctx->config->fn_is_safe &&
        !ctx->config->fn_is_safe(ctx, UDS_SID_WRITE_MEM_BY_ADDR, data, len)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    int res = ctx->config->fn_mem_write(ctx, addr, size, &data[consumed]);
    if (res < 0) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_MEM_BY_ADDR, (uint8_t) - (int32_t) res);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_WRITE_MEM_BY_ADDR + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = format;

    /* C-20: ISO 14229-1: Server shall echo the address and size if successfully written */
    for (uint16_t i = 0u; i < (uint16_t) ((uint16_t) addr_len + (uint16_t) size_len); i++) {
        ctx->config->tx_buffer[2u + i] = data[2u + i];
    }

    return uds_send_response(ctx, (uint16_t) (2u + (uint16_t) addr_len + (uint16_t) size_len));
}
