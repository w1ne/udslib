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
    if (len < 3) {
        return uds_send_nrc(ctx, 0x34, 0x13);
    }

    uint8_t fmt = data[2]; /* 0x34 [DFI] [ALFID] [Addr...] [Size...] */ 
    /* Wait, ISO 14229-1 Table 415: 
       Byte 1: SID (0x34)
       Byte 2: dataFormatIdentifier (0x00 usually)
       Byte 3: addressAndLengthFormatIdentifier
       Byte 4..: Address
       Byte ...: Size
       
       My previous code assumed data[1] is ALFID? 
       No, previous code assumed hardcoded offsets.
       Let's check spec carefully.
       Request Download:
       [34] [dataFormatIdentifier] [addressAndLengthFormatIdentifier] [MemoryAddress] [MemorySize]
       So ALFID is data[2]. 
    */
    
    uint8_t dfi = data[1];
    uint8_t alfid = data[2];
    
    uint8_t addr_len = alfid & 0x0F;
    uint8_t size_len = (alfid >> 4) & 0x0F;

    if (addr_len == 0 || size_len == 0 || addr_len > 4 || size_len > 4) {
        return uds_send_nrc(ctx, 0x34, 0x31); /* Request Out Of Range (Format) */
    }

    if (len < (3 + addr_len + size_len)) {
        return uds_send_nrc(ctx, 0x34, 0x13);
    }

    uint32_t addr = 0;
    for (int i = 0; i < addr_len; i++) {
        addr = (addr << 8) | data[3 + i];
    }

    uint32_t size = 0;
    for (int i = 0; i < size_len; i++) {
        size = (size << 8) | data[3 + addr_len + i];
    }

    if (!ctx->config->fn_request_download) {
        return uds_send_nrc(ctx, 0x34, 0x22);
    }

    int res = ctx->config->fn_request_download(ctx, addr, size);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x34, (uint8_t)(-res));
    }

    /* Initialize Sequence Counter for 0x36 */
    ctx->transfer_sequence = 1;

    ctx->config->tx_buffer[0] = 0x74;
    ctx->config->tx_buffer[1] = 0x20; 
    ctx->config->tx_buffer[2] = 0x04; /* Max Block Size (Placeholder) */ // Should be configurable?
    ctx->config->tx_buffer[3] = 0x02; /* 0x0402 = 1026 bytes? No, usually Byte 2 is High, Byte 3 is Low? */
    /* ISO: maxNumberOfBlockLength. format based on data[1]? No.
       Response: [74] [LenFmt] [MaxLen...]
       If LenFmt=0x20 -> 2 bytes length. 
       Let's assume 1024 (0x0400).
    */
    ctx->config->tx_buffer[2] = (ctx->config->rx_buffer_size >> 8) & 0xFF;
    ctx->config->tx_buffer[3] = (ctx->config->rx_buffer_size & 0xFF);

    return uds_send_response(ctx, 4);
}

int uds_internal_handle_transfer_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return uds_send_nrc(ctx, 0x36, 0x13);
    }

    uint8_t sequence = data[1];

    /* C-13: Verify Block Sequence Counter */
    if (sequence != ctx->transfer_sequence) {
        return uds_send_nrc(ctx, 0x36, 0x73); /* Wrong Block Sequence Counter */
    }

    if (!ctx->config->fn_transfer_data) {
        return uds_send_nrc(ctx, 0x36, 0x22);
    }

    int res = ctx->config->fn_transfer_data(ctx, sequence, &data[2], len - 2);
    if (res < 0) {
        return uds_send_nrc(ctx, 0x36, (uint8_t)(-res));
    }

    /* Increment Sequence (Rollover handled by uint8_t overflow naturally) */
    ctx->transfer_sequence++;

    ctx->config->tx_buffer[0] = 0x76;
    ctx->config->tx_buffer[1] = sequence; /* Echo sequence */
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
