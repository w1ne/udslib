/**
 * @file uds_service_mem.c
 * @brief Memory Services Implementation (0x23, 0x3D)
 */

#include "uds_internal.h"
#include <string.h>

/**
 * @brief Helper to parse Address and Length based on format byte
 */
static bool parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr, uint32_t *size, uint16_t *consumed)
{
    uint8_t addr_len = format & 0x0F;
    uint8_t size_len = (format >> 4) & 0x0F;

    /* C-09: Check Invalid ALFID (0 length) */
    if (addr_len == 0 || size_len == 0 || addr_len > 4 || size_len > 4) {
        return false; 
    }

    if (len < (2 + addr_len + size_len)) {
        return false;
    }

    *addr = 0;
    for (int i = 0; i < addr_len; i++) {
        *addr = (*addr << 8) | data[2 + i];
    }

    *size = 0;
    for (int i = 0; i < size_len; i++) {
        *size = (*size << 8) | data[2 + addr_len + i];
    }

    if (consumed) {
        *consumed = 2 + addr_len + size_len;
    }

    return true;
}

int uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3) {
        return uds_send_nrc(ctx, 0x23, 0x13); /* Incorrect Message Length Or Invalid Format */
    }

    uint8_t format = data[1];
    uint32_t addr, size;
    uint16_t consumed;

    if (!parse_addr_len(data, len, format, &addr, &size, &consumed)) {
        return uds_send_nrc(ctx, 0x23, 0x31); /* Request Out Of Range (Format) */
    }

    if (!ctx->config->fn_mem_read) {
        return uds_send_nrc(ctx, 0x23, 0x11); /* Service Not Supported */
    }

    if (size > (uint32_t)(ctx->config->tx_buffer_size - 1)) {
        return uds_send_nrc(ctx, 0x23, 0x14); /* Response Too Long */
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
    uint16_t consumed;

    if (!parse_addr_len(data, len, format, &addr, &size, &consumed)) {
        return uds_send_nrc(ctx, 0x3D, 0x31);
    }

    if ((uint64_t)addr + size > 0xFFFFFFFF) {
        return uds_send_nrc(ctx, 0x3D, 0x31); /* Address Overflow */
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

    /* C-20: Echo Address and Size */
    /* Request structure: [3D] [FMT] [ADDR...] [SIZE...] [DATA...] */
    /* Consumed = 2 + ADDR_LEN + SIZE_LEN */
    /* Response structure: [7D] [FMT] [ADDR...] [SIZE...] */
    
    ctx->config->tx_buffer[0] = 0x7D;
    memcpy(&ctx->config->tx_buffer[1], &data[1], consumed - 1);
    
    return uds_send_response(ctx, consumed);
}

