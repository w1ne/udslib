/**
 * @file uds_service_data.c
 * @brief Read Data By ID (0x22) & Write Data By ID (0x2E)
 */

#include <string.h>
#include "uds_internal.h"

int uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint16_t tx_len = 1u; /* SID 0x62 set later */
    uint16_t i = 1u;
    bool any_error = false;

    while (i + 1u < len) {
        uint16_t did = (uint16_t) (((uint16_t) data[i] << 8u) | (uint16_t) data[i + 1u]);
        const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

        if (entry != NULL) {
            if ((uint32_t) tx_len + (uint32_t) entry->size >
                (uint32_t) ctx->config->tx_buffer_size) {
                return uds_send_nrc(ctx, UDS_SID_READ_DATA_BY_ID,
                                    UDS_NRC_RESPONSE_TOO_LONG); /* Response Too Long */
            }

            ctx->config->tx_buffer[tx_len] = (uint8_t) ((did >> 8u) & 0xFFu);
            tx_len++;
            ctx->config->tx_buffer[tx_len] = (uint8_t) (did & 0xFFu);
            tx_len++;

            if (entry->read != NULL) {
                int res = entry->read(ctx, did, &ctx->config->tx_buffer[tx_len], entry->size);
                if (res >= 0) {
                    tx_len += (uint16_t) entry->size;
                }
                else {
                    return uds_send_nrc(ctx, UDS_SID_READ_DATA_BY_ID, (uint8_t) - (int32_t) res);
                }
            }
            else if (entry->storage != NULL) {
                memcpy(&ctx->config->tx_buffer[tx_len], entry->storage, entry->size);
                tx_len += (uint16_t) entry->size;
            }
            else {
                any_error = true;
                break;
            }
        }
        else {
            any_error = true;
            break;
        }
        i += 2u;
    }

    if (any_error) {
        return uds_send_nrc(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }
    else {
        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_OFFSET);
        return uds_send_response(ctx, tx_len);
    }
}

int uds_internal_handle_write_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_LENGTH);
    }
    uint16_t did = (uint16_t) (((uint16_t) data[1] << 8u) | (uint16_t) data[2]);
    const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

    if ((entry != NULL) && (len == (uint16_t) (3u + (uint16_t) entry->size))) {
        bool write_ok = false;
        if (entry->write != NULL) {
            int res = entry->write(ctx, did, &data[3], entry->size);
            if (res == 0) {
                write_ok = true;
            }
        }
        else if (entry->storage != NULL) {
            memcpy(entry->storage, &data[3], entry->size);
            write_ok = true;
        }

        if (write_ok) {
            ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_OFFSET);
            ctx->config->tx_buffer[1] = data[1];
            ctx->config->tx_buffer[2] = data[2];
            return uds_send_response(ctx, 3u);
        }
    }
    return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}
