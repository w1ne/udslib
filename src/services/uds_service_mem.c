/**
 * @file uds_service_mem.c
 * @brief Memory Services Implementation (0x23, 0x3D)
 */

#include "uds_internal.h"
#include <string.h>

int uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3) {
        return uds_send_nrc(ctx, 0x23, 0x13); /* Incorrect Message Length Or Invalid Format */
    }

    uint8_t format = data[1];
    uint32_t addr, size;

    if (!uds_internal_parse_addr_len(&data[2], len - 2, format, &addr, &size)) {
        return uds_send_nrc(ctx, 0x23, 0x13);
    }

    if (!ctx->config->fn_mem_read) {
        return uds_send_nrc(ctx, 0x23, 0x11); /* Service Not Supported */
    }

    if (size > (uint32_t)(ctx->config->tx_buffer_size - 1)) {
        return uds_send_nrc(ctx, 0x23, 0x31); /* Request Out Of Range */
    }

    if ((uint64_t)addr + size > 0xFFFFFFFF) {
        return uds_send_nrc(ctx, 0x23, 0x31); /* Address Overflow */
    }

    int res = ctx->config->fn_mem_read(ctx, addr, size, &ctx->config->tx_buffer[1]);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x23, (uint8_t)(-res));
    }

    ctx->config->tx_buffer[0] = 0x63;
    return uds_send_response(ctx, size + 1);
}

int uds_internal_handle_write_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3) {
        return uds_send_nrc(ctx, 0x3D, 0x13);
    }

    uint8_t format = data[1];
    uint32_t addr, size;
    uint8_t addr_len = format & 0x0F;
    uint8_t size_len = (format >> 4) & 0x0F;
    uint16_t consumed = 2 + addr_len + size_len;

    if (!uds_internal_parse_addr_len(&data[2], len - 2, format, &addr, &size)) {
        return uds_send_nrc(ctx, 0x3D, 0x13);
    }

    if (len != (consumed + size)) {
        return uds_send_nrc(ctx, 0x3D, 0x13);
    }

    if (!ctx->config->fn_mem_write) {
        return uds_send_nrc(ctx, 0x3D, 0x11);
    }

    int res = ctx->config->fn_mem_write(ctx, addr, size, &data[consumed]);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x3D, (uint8_t)(-res));
    }

    ctx->config->tx_buffer[0] = 0x7D;
    ctx->config->tx_buffer[1] = format;
    
    /* ISO 14229-1: Server shall echo the address and size if successfully written */
    for (int i = 0; i < (addr_len + size_len); i++) {
        ctx->config->tx_buffer[2 + i] = data[2 + i];
    }
    
    return uds_send_response(ctx, 2 + addr_len + size_len);
}
