/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_data.c
 * @brief Read Data By ID (0x22) & Write Data By ID (0x2E)
 */

#include <string.h>
#include "uds_internal.h"

void uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                         uds_result_t *out)
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
                !(uds_internal_session_bit(ctx->session.active) & entry->session_mask)) {
                any_error = true;
                nrc_code = UDS_NRC_REQUEST_OUT_OF_RANGE; /* 0x31 per ISO 14229-1 */
                break;
            }

            /* Security Check */
            if ((entry->security_mask != 0u) &&
                !((1u << ctx->security.level) & entry->security_mask)) {
                any_error = true;
                nrc_code = UDS_NRC_SECURITY_ACCESS_DENIED;
                break;
            }

            /* C-12: Buffer Overflow Vulnerability Check */
            if ((uint32_t) tx_len + (uint32_t) entry->size + 2u > /* +2 for DID ID */
                (uint32_t) ctx->config->tx_buffer_size) {
                uds_nrc(out, UDS_NRC_RESPONSE_TOO_LONG);
                return;
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
                    uds_nrc(out, (uint8_t) - (int32_t) res);
                    return;
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
        uds_nrc(out, nrc_code);
        return;
    }
    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_OFFSET);
    uds_ok(out, tx_len);
}

void uds_internal_handle_read_scaling(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out)
{
    (void) len; /* min_len 3 enforced by the dispatcher */

    /* No reader -> the DID's scaling information is not supported. */
    if (ctx->config->fn_read_scaling == NULL) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    uint16_t did = (uint16_t) (((uint16_t) data[1] << 8u) | (uint16_t) data[2]);

    uint8_t *tx = ctx->config->tx_buffer;
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 3u);

    int written = ctx->config->fn_read_scaling(ctx, did, &tx[3], max_payload);
    if (written < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) written);
        return;
    }

    tx[0] = (uint8_t) (UDS_SID_READ_SCALING + UDS_RESPONSE_OFFSET);
    tx[1] = data[1];
    tx[2] = data[2];
    uds_ok(out, (uint16_t) ((uint16_t) written + 3u));
}

void uds_internal_handle_dynamic_did(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                     uds_result_t *out)
{
    /* Sub-function range is enforced by the dispatcher (mask_sub_2C). */
    uint8_t subfn = (uint8_t) (data[1] & 0x7Fu);

    /* defineByIdentifier (0x01) / defineByMemoryAddress (0x02) need a
     * dynamicallyDefinedDataIdentifier (2 bytes). clear (0x03) may omit it. */
    bool has_did = (len >= 4u);
    if (((subfn == 0x01u) || (subfn == 0x02u)) && !has_did) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (ctx->config->fn_dynamic_did == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    uint16_t defined_did = 0u;
    if (has_did) {
        defined_did = (uint16_t) (((uint16_t) data[2] << 8u) | (uint16_t) data[3]);
    }

    int res = ctx->config->fn_dynamic_did(ctx, subfn, defined_did, &data[2], (uint16_t) (len - 2u));
    if (res < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_DYNAMIC_DID + UDS_RESPONSE_OFFSET);
    tx[1] = subfn;
    if (has_did) {
        tx[2] = data[2];
        tx[3] = data[3];
        uds_ok(out, 4u);
        return;
    }
    uds_ok(out, 2u);
}

void uds_internal_handle_write_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                          uds_result_t *out)
{
    if (len < 3u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    uint16_t did = (uint16_t) (((uint16_t) data[1] << 8u) | (uint16_t) data[2]);
    const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

    if (entry == NULL) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    /* C-18: Security & Session Validation per DID */
    /* Session Check */
    if ((entry->session_mask != 0u) &&
        !(uds_internal_session_bit(ctx->session.active) & entry->session_mask)) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    /* Security Check */
    if ((entry->security_mask != 0u) && !((1u << ctx->security.level) & entry->security_mask)) {
        uds_nrc(out, UDS_NRC_SECURITY_ACCESS_DENIED);
        return;
    }

    /* C-11: Length Check Failure (NRC 0x13) */
    if (len != (uint16_t) (3u + (uint16_t) entry->size)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
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
        uds_ok(out, 3u);
        return;
    }

    uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

void uds_internal_handle_periodic_read(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                       uds_result_t *out)
{
    /* ISO 14229-1: 0x2A [transmissionMode] [periodicDataIdentifier...] */
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t mode = data[1] & 0x0Fu; /* 1: Fast, 2: Medium, 3: Slow, 4: Stop */

    if (mode == 0x04u) {
        /* Stop Sending */
        if (len == 2u) {
            /* Stop all. Element-wise clear: periodic_ids is volatile (read by the
             * uds_process() scheduler), which memset() cannot target. */
            for (uint8_t j = 0u;
                 j < (sizeof(ctx->server.periodic_ids) / sizeof(ctx->server.periodic_ids[0]));
                 j++) {
                ctx->server.periodic_ids[j] = 0u;
            }
            ctx->server.periodic_count = 0u;
        }
        else {
            /* Stop specific IDs */
            for (uint16_t i = 2u; i < len; i++) {
                uint8_t id = data[i];
                for (uint8_t j = 0u; j < 8u; j++) {
                    if (ctx->server.periodic_ids[j] == id) {
                        ctx->server.periodic_ids[j] = 0u;
                        ctx->server.periodic_count--;
                    }
                }
            }
        }
        ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_BY_PER_ID + UDS_RESPONSE_OFFSET);
        uds_ok(out, 1u);
        return;
    }

    if (mode < 0x01u || mode > 0x03u) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    /* Cannot schedule periodic reads without a reader callback. */
    if (ctx->config->fn_periodic_read == NULL) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    /* Add/Update Periodic IDs */
    for (uint16_t i = 2u; i < len; i++) {
        uint8_t id = data[i];
        bool found = false;
        for (uint8_t j = 0u; j < 8u; j++) {
            if (ctx->server.periodic_ids[j] == id) {
                ctx->server.periodic_rates[j] = mode;
                found = true;
                break;
            }
        }
        if (!found) {
            if (ctx->server.periodic_count >= 8u) {
                uds_nrc(out, UDS_NRC_RESPONSE_TOO_LONG);
                return;
            }
            for (uint8_t j = 0u; j < 8u; j++) {
                if (ctx->server.periodic_ids[j] == 0u) {
                    ctx->server.periodic_ids[j] = id;
                    ctx->server.periodic_rates[j] = mode;
                    ctx->server.periodic_timers[j] =
                        ctx->config->get_time_ms(); /* Start immediately or after interval */
                    ctx->server.periodic_count++;
                    break;
                }
            }
        }
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_BY_PER_ID + UDS_RESPONSE_OFFSET);
    uds_ok(out, 1u);
}
