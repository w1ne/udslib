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
    uint8_t nrc_code = UDS_NRC_REQUEST_OUT_OF_RANGE;

    while (i + 1u < len) {
        uint16_t did = (uint16_t) (((uint16_t) data[i] << 8u) | (uint16_t) data[i + 1u]);
        const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

        if (entry != NULL) {
            /* C-18: Security & Session Validation per DID */
            /* Session Check */
            if ((entry->session_mask != 0u) &&
                !((1u << (ctx->active_session - 1u)) & entry->session_mask)) {
                any_error = true;
                nrc_code = UDS_NRC_REQUEST_OUT_OF_RANGE; /* 0x31 per ISO 14229-1 */
                break;
            }

            /* Security Check */
            if ((entry->security_mask != 0u) &&
                !((1u << ctx->security_level) & entry->security_mask)) {
                any_error = true;
                nrc_code = UDS_NRC_SECURITY_ACCESS_DENIED;
                break;
            }

            /* C-12: Buffer Overflow Vulnerability Check */
            if ((uint32_t) tx_len + (uint32_t) entry->size + 2u > /* +2 for DID ID */
                (uint32_t) ctx->config->tx_buffer_size) {
                return uds_send_nrc(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
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
                /* No read handler and no storage - invalid DID config */
                any_error = true;
                nrc_code = UDS_NRC_CONDITIONS_NOT_CORRECT;
                break;
            }
        }
        else {
            any_error = true; /* 0x31 */
            nrc_code = UDS_NRC_REQUEST_OUT_OF_RANGE;
            break;
        }
        i += 2u;
    }

    if (any_error) {
        return uds_send_nrc(ctx, UDS_SID_READ_DATA_BY_ID, nrc_code);
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

    if (entry == NULL) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* C-18: Security & Session Validation per DID */
    /* Session Check */
    if ((entry->session_mask != 0u) &&
        !((1u << (ctx->active_session - 1u)) & entry->session_mask)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* Security Check */
    if ((entry->security_mask != 0u) && !((1u << ctx->security_level) & entry->security_mask)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SECURITY_ACCESS_DENIED);
    }

    /* C-11: Length Check Failure (NRC 0x13) */
    if (len != (uint16_t) (3u + (uint16_t) entry->size)) {
        return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_LENGTH);
    }

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

    return uds_send_nrc(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

int uds_internal_handle_periodic_read(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* ISO 14229-1: 0x2A [transmissionMode] [periodicDataIdentifier...] */
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_READ_BY_PER_ID, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t mode = data[1] & 0x0Fu; /* 1: Fast, 2: Medium, 3: Slow, 4: Stop */

    if (mode == 0x04u) {
        /* Stop Sending */
        if (len == 2u) {
            /* Stop all */
            memset(ctx->periodic_ids, 0, sizeof(ctx->periodic_ids));
            ctx->periodic_count = 0u;
        }
        else {
            /* Stop specific IDs */
            for (uint16_t i = 2u; i < len; i++) {
                uint8_t id = data[i];
                for (uint8_t j = 0u; j < 8u; j++) {
                    if (ctx->periodic_ids[j] == id) {
                        ctx->periodic_ids[j] = 0u;
                        ctx->periodic_count--;
                    }
                }
            }
        }
        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_BY_PER_ID + UDS_RESPONSE_OFFSET);
        return uds_send_response(ctx, 1u);
    }

    if (mode < 0x01u || mode > 0x03u) {
        return uds_send_nrc(ctx, UDS_SID_READ_BY_PER_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* Add/Update Periodic IDs */
    for (uint16_t i = 2u; i < len; i++) {
        uint8_t id = data[i];
        bool found = false;
        for (uint8_t j = 0u; j < 8u; j++) {
            if (ctx->periodic_ids[j] == id) {
                ctx->periodic_rates[j] = mode;
                found = true;
                break;
            }
        }
        if (!found) {
            if (ctx->periodic_count >= 8u) {
                return uds_send_nrc(ctx, UDS_SID_READ_BY_PER_ID, UDS_NRC_RESPONSE_TOO_LONG);
            }
            for (uint8_t j = 0u; j < 8u; j++) {
                if (ctx->periodic_ids[j] == 0u) {
                    ctx->periodic_ids[j] = id;
                    ctx->periodic_rates[j] = mode;
                    ctx->periodic_timers[j] = ctx->config->get_time_ms(); /* Start immediately or after interval */
                    ctx->periodic_count++;
                    break;
                }
            }
        }
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_BY_PER_ID + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, 1u);
}
