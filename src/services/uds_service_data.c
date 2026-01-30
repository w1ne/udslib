/**
 * @file uds_service_data.c
 * @brief Read Data By ID (0x22) & Write Data By ID (0x2E)
 */

#include <string.h>
#include "uds_internal.h"

int uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint16_t tx_len = 1; /* SID 0x62 set later */
    uint16_t i = 1;
    bool any_error = false;

    while (i + 1 < len) {
        uint16_t did = (data[i] << 8) | data[i + 1];
        const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

        if (entry) {
            ctx->config->tx_buffer[tx_len++] = (did >> 8) & 0xFF;
            ctx->config->tx_buffer[tx_len++] = did & 0xFF;

            if (entry->read) {
                int res = entry->read(ctx, did, &ctx->config->tx_buffer[tx_len], entry->size);
                if (res >= 0) {
                    tx_len += entry->size;
                } else {
                    any_error = true;
                    break;
                }
            } else if (entry->storage) {
                memcpy(&ctx->config->tx_buffer[tx_len], entry->storage, entry->size);
                tx_len += entry->size;
            } else {
                any_error = true;
                break;
            }
        } else {
            any_error = true;
            break;
        }
        i += 2;
    }

    if (any_error) {
        return uds_send_nrc(ctx, 0x22, 0x31);
    } else {
        ctx->config->tx_buffer[0] = 0x62;
        return uds_send_response(ctx, tx_len);
    }
}

int uds_internal_handle_write_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint16_t did = (data[1] << 8) | data[2];
    const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

    if (entry && (len == (3 + entry->size))) {
        bool write_ok = false;
        if (entry->write) {
            int res = entry->write(ctx, did, &data[3], entry->size);
            if (res == 0) {
                write_ok = true;
            }
        } else if (entry->storage) {
            memcpy(entry->storage, &data[3], entry->size);
            write_ok = true;
        }

        if (write_ok) {
            ctx->config->tx_buffer[0] = 0x6E;
            ctx->config->tx_buffer[1] = data[1];
            ctx->config->tx_buffer[2] = data[2];
            return uds_send_response(ctx, 3);
        }
    }
    return uds_send_nrc(ctx, 0x2E, 0x31);
}
