/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_maintenance.c
 * @brief Maintenance Services: ECU Reset (0x11), Comm Control (0x28), and DTC Management (0x14,
 * 0x19, 0x85)
 */

#include "uds_internal.h"
#include "uds/uds_dtc.h"
#include <string.h>

int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_ECU_RESET, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    /* ISO 14229-1:2013 Table 21: 0x01-0x05 */
    if ((sub < 0x01u) || (sub > 0x05u)) {
        return uds_send_nrc(ctx, UDS_SID_ECU_RESET, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Process Reset */
    if (ctx->config->fn_reset) {
        ctx->config->fn_reset(ctx, sub);
    }

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ECU_RESET + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    /* C-11: Minimum length is 3 bytes (SI+Control+Comm) */
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t ctrl_type = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);
    uint8_t comm_type = data[2];

    if (ctrl_type > 0x05u) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    /* ISO 14229-1: communicationType lower nibble must be 1, 2, or 3 */
    uint8_t type_nibble = (uint8_t) (comm_type & 0x0Fu);
    if ((type_nibble == 0u) || (type_nibble > 3u)) {
        return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Check App Callback */
    if (ctx->config->fn_comm_control) {
        int ret = ctx->config->fn_comm_control(ctx, ctrl_type, comm_type);
        if (ret != UDS_OK) {
            return uds_send_nrc(ctx, UDS_SID_COMM_CONTROL, (uint8_t) - (int32_t) ret);
        }
    }

    ctx->comm_state = ctrl_type;
    uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_COMM_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = ctrl_type;

    return uds_send_response(ctx, 2u);
}

int uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC,
                            UDS_NRC_INCORRECT_LENGTH); /* Incorrect Msg Length */
    }

    if (!ctx->config->fn_dtc_clear) {
        return uds_send_nrc(
            ctx, UDS_SID_CLEAR_DTC,
            UDS_NRC_CONDITIONS_NOT_CORRECT); /* Conditions Not Correct (No clear hook) */
    }

    uint32_t group = (uint32_t) ((uint32_t) data[1] << 16u) |
                     (uint32_t) ((uint32_t) data[2] << 8u) | (uint32_t) data[3];
    int res = ctx->config->fn_dtc_clear(ctx, group);

    if (res != UDS_OK) {
        return uds_send_nrc(ctx, UDS_SID_CLEAR_DTC, (uint8_t) - (int32_t) res);
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CLEAR_DTC + UDS_RESPONSE_OFFSET);
    return uds_send_response(ctx, 1u);
}

/** Max DTC records gathered per ReadDTCInformation response (stack-bounded). */
#ifndef UDS_DTC_LIST_BATCH
#define UDS_DTC_LIST_BATCH 32u
#endif

/* Format reportNumberOfDTCByStatusMask (0x01), reportDTCByStatusMask (0x02),
 * and reportSupportedDTC (0x0A). The library owns the ISO 14229-1 wire layout;
 * the application only supplies records via fn_dtc_list. */
static int uds_internal_dtc_by_status(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                      uint16_t len, bool suppress_pos_resp)
{
    (void) len;
    /* 0x0A reports every DTC (status mask 0); 0x01/0x02 filter by data[2]. */
    uint8_t status_mask = (sub == 0x0Au) ? 0u : data[2];

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    if (sub == 0x01u) {
        int count = ctx->config->fn_dtc_list(ctx, status_mask, NULL, 0u);
        if (count < 0) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) count);
        }
        if (suppress_pos_resp) {
            return UDS_OK;
        }
        tx[3] = ctx->config->dtc_format_id;
        tx[4] = (uint8_t) (((uint32_t) count >> 8) & 0xFFu);
        tx[5] = (uint8_t) ((uint32_t) count & 0xFFu);
        return uds_send_response(ctx, 6u);
    }

    /* 0x02 / 0x0A: emit [DTC(3) | status(1)] per matching record. */
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    uint16_t room = (uint16_t) ((ctx->config->tx_buffer_size - 3u) / 4u);
    uint16_t cap = (room < UDS_DTC_LIST_BATCH) ? room : (uint16_t) UDS_DTC_LIST_BATCH;

    int total = ctx->config->fn_dtc_list(ctx, status_mask, recs, cap);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > cap) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }

    uint16_t n = (uint16_t) total;
    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < n; i++) {
        tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 3u] = recs[i].status;
        pos = (uint16_t) (pos + 4u);
    }
    return uds_send_response(ctx, pos);
}

/* Format reportDTCSnapshotRecordByDTCNumber (0x04) and
 * reportDTCExtendedDataRecordByDTCNumber (0x06). The library validates the
 * request, echoes the DTC, and frames the response; the application writes
 * the record payload (statusOfDTC + records) via fn_dtc_snapshot/extdata. */
static int uds_internal_dtc_record(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data, uint16_t len,
                                   bool suppress_pos_resp)
{
    /* Request: SID, sub, DTC(3), recordNumber(1) -> 6 bytes. */
    if (len < 6u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    uint32_t dtc = (uint32_t) ((uint32_t) data[2] << 16) | (uint32_t) ((uint32_t) data[3] << 8) |
                   (uint32_t) data[4];
    uint8_t record_num = data[5];

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 5u);

    int written;
    if (sub == 0x04u) {
        written = ctx->config->fn_dtc_snapshot(ctx, dtc, record_num, &tx[5], max_payload);
    }
    else {
        written = ctx->config->fn_dtc_extdata(ctx, dtc, record_num, &tx[5], max_payload);
    }

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) written);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }

    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = data[2];
    tx[3] = data[3];
    tx[4] = data[4];
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 5u));
}

/* Format reportNumberOfDTCBySeverityMaskRecord (0x07) and
 * reportDTCBySeverityMaskRecord (0x08). Request: SID, sub, sevMask, statMask. */
static int uds_internal_dtc_by_severity(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                        uint16_t len, bool suppress_pos_resp)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t sev_mask = data[2];
    uint8_t status_mask = data[3];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, status_mask, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    uint16_t n = (uint16_t) total;

    if (sub == 0x07u) {
        uint16_t count = 0u;
        for (uint16_t i = 0u; i < n; i++) {
            if ((sev_mask == 0u) || ((recs[i].severity & sev_mask) != 0u)) {
                count++;
            }
        }
        if (suppress_pos_resp) {
            return UDS_OK;
        }
        tx[3] = ctx->config->dtc_format_id;
        tx[4] = (uint8_t) ((count >> 8) & 0xFFu);
        tx[5] = (uint8_t) (count & 0xFFu);
        return uds_send_response(ctx, 6u);
    }

    /* 0x08: [severity functionalUnit DTC(3) status] per matching record. */
    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < n; i++) {
        if ((sev_mask != 0u) && ((recs[i].severity & sev_mask) == 0u)) {
            continue;
        }
        if ((uint16_t) (pos + 6u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = recs[i].severity;
        tx[pos + 1u] = recs[i].functional_unit;
        tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 3u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 4u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 5u] = recs[i].status;
        pos = (uint16_t) (pos + 6u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportSeverityInformationOfDTC (0x09). Request: SID, sub, DTC(3). */
static int uds_internal_dtc_severity_info(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                          uint16_t len, bool suppress_pos_resp)
{
    if (len < 5u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint32_t want = (uint32_t) ((uint32_t) data[2] << 16) | (uint32_t) ((uint32_t) data[3] << 8) |
                    (uint32_t) data[4];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].dtc == want) {
            tx[pos] = recs[i].severity;
            tx[pos + 1u] = recs[i].functional_unit;
            tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
            tx[pos + 3u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
            tx[pos + 4u] = (uint8_t) (recs[i].dtc & 0xFFu);
            tx[pos + 5u] = recs[i].status;
            pos = (uint16_t) (pos + 6u);
            break;
        }
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

int uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    /* C-10: DTC Status Mask Requirement */
    bool req_mask = (sub == 0x01u || sub == 0x02u || sub == 0x07u || sub == 0x08u || sub == 0x0Fu ||
                     sub == 0x11u || sub == 0x12u || sub == 0x13u);

    if (req_mask && len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    /* Structured subfunctions: library formats the ISO wire layout when the
     * application provides fn_dtc_list. Falls through to the raw fn_dtc_read
     * path below when fn_dtc_list is NULL (back-compat). */
    if (((sub == 0x01u) || (sub == 0x02u) || (sub == 0x0Au)) &&
        (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_by_status(ctx, sub, data, len, suppress_pos_resp);
    }

    if ((sub == 0x04u) && (ctx->config->fn_dtc_snapshot != NULL)) {
        return uds_internal_dtc_record(ctx, sub, data, len, suppress_pos_resp);
    }

    if ((sub == 0x06u) && (ctx->config->fn_dtc_extdata != NULL)) {
        return uds_internal_dtc_record(ctx, sub, data, len, suppress_pos_resp);
    }

    if (((sub == 0x07u) || (sub == 0x08u)) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_by_severity(ctx, sub, data, len, suppress_pos_resp);
    }

    if ((sub == 0x09u) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_severity_info(ctx, sub, data, len, suppress_pos_resp);
    }

    if (!ctx->config->fn_dtc_read) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    /* Pass subfunction and optional mask (data[2]) to the application */
    int written = ctx->config->fn_dtc_read(ctx, sub, out_payload, max_payload);
    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) written);
    }

    if (suppress_pos_resp) {
        return UDS_OK;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 2u));
}

int uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    if ((sub != 0x01u) && (sub != 0x02u)) { /* ON / OFF */
        return uds_send_nrc(ctx, UDS_SID_CONTROL_DTC_SETTING, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    /* Process DTC Setting Control (usually global flag in ctx or config) */
    /* ... application should probably handle this via a hook if needed ... */

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CONTROL_DTC_SETTING + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    return uds_send_response(ctx, 2u);
}
