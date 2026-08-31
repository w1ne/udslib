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

void uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                   uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);

    /* ISO 14229-1:2013 Table 21: 0x01-0x05 */
    if ((sub < 0x01u) || (sub > 0x05u)) {
        uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    /* ISO 14229-1 (§ ECUReset): "The ECUReset positive response message (if
     * required) shall be sent before the reset is executed in the server(s)."
     * Build the positive response payload now and queue the reset as a deferred
     * post-TX action; the framework promotes it once the response has been
     * emitted (or suppressed) and uds_process() runs it — never via a busy-wait,
     * so it is safe on any substrate (issue #88/#98). */
    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_ECU_RESET + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    ctx->scratch.posttx_kind = (uint8_t) UDS_POSTTX_RESET;
    ctx->scratch.posttx_arg = sub;

    /* ISO 14229-1: the enableRapidPowerShutDown (0x04) positive response
     * carries an additional powerDownTime byte; all other reset types
     * respond with SID + resetType only. */
    uint16_t resp_len = 2u;
    if (sub == (uint8_t) UDS_RESET_ENABLE_RAPID_SHUTDOWN) {
        ctx->config->tx_buffer[2] = ctx->config->power_down_time;
        resp_len = 3u;
    }
    uds_ok(out, resp_len);
}

void uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out)
{
    /* C-11: Minimum length is 3 bytes (SI+Control+Comm) */
    if (len < 3u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub_raw = data[1];
    uint8_t ctrl_type = (uint8_t) (sub_raw & 0x7Fu);
    uint8_t comm_type = data[2];

    if (ctrl_type > 0x05u) {
        uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    /* The enhanced-address sub-functions (0x04/0x05) carry a 2-byte
     * nodeIdentificationNumber after communicationType, so the message is at
     * least 5 bytes. ISO 14229-1, CommunicationControl request layout. */
    bool enhanced = (ctrl_type == UDS_COMM_ENABLE_RX_DISABLE_TX_ENH) ||
                    (ctrl_type == UDS_COMM_ENABLE_RX_TX_ENH);
    if (enhanced && (len < 5u)) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    uint16_t node_id = enhanced ? (uint16_t) (((uint16_t) data[3] << 8) | (uint16_t) data[4]) : 0u;

    /* ISO 14229-1: communicationType lower nibble must be 1, 2, or 3 */
    uint8_t type_nibble = (uint8_t) (comm_type & 0x0Fu);
    if ((type_nibble == 0u) || (type_nibble > 3u)) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    /* Check App Callback */
    if (ctx->config->fn_comm_control) {
        int ret = ctx->config->fn_comm_control(ctx, ctrl_type, comm_type, node_id);
        if (ret != UDS_OK) {
            uds_nrc(out, (uint8_t) - (int32_t) ret);
            return;
        }
    }

    ctx->session.comm_state = ctrl_type;
    uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_COMM_CONTROL + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = ctrl_type;
    uds_ok(out, 2u);
}

void uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                   uds_result_t *out)
{
    if (len < 4u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (!ctx->config->fn_dtc_clear) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    uint32_t group = (uint32_t) ((uint32_t) data[1] << 16u) |
                     (uint32_t) ((uint32_t) data[2] << 8u) | (uint32_t) data[3];
    int res = ctx->config->fn_dtc_clear(ctx, group);

    if (res != UDS_OK) {
        uds_nrc(out, (uint8_t) - (int32_t) res);
        return;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CLEAR_DTC + UDS_RESPONSE_OFFSET);
    uds_ok(out, 1u);
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
        ctx->scratch.suppress_pos_resp = true;
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
        ctx->scratch.suppress_pos_resp = true;
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
        ctx->scratch.suppress_pos_resp = true;
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
        ctx->scratch.suppress_pos_resp = true;
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

/* Format reportDTCFaultDetectionCounter (0x14). Request: SID, sub.
 * Reports DTCs whose fault-detection counter is in progress (1..0x7E). */
static int uds_internal_dtc_fault_counter(uds_ctx_t *ctx, uint8_t sub, bool suppress_pos_resp)
{
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;

    uint16_t pos = 2u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        int8_t fdc = recs[i].fault_detection_counter;
        if ((fdc >= 1) && (fdc <= 0x7E)) {
            if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
                return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
            }
            tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
            tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
            tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
            tx[pos + 3u] = (uint8_t) fdc;
            pos = (uint16_t) (pos + 4u);
        }
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportWWHOBDDTCByMaskRecord (0x42).
 * Request: SID, sub, FGID, statusMask, severityMask. */
static int uds_internal_dtc_wwhobd(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data, uint16_t len,
                                   bool suppress_pos_resp)
{
    if (len < 5u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t fgid = data[2];
    uint8_t status_mask = data[3];
    uint8_t sev_mask = data[4];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, status_mask, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = fgid;
    tx[3] = ctx->config->dtc_status_availability_mask;
    tx[4] = ctx->config->dtc_severity_availability_mask;
    tx[5] = ctx->config->dtc_format_id;

    uint16_t pos = 6u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].functional_group != fgid) {
            continue;
        }
        if ((sev_mask != 0u) && ((recs[i].severity & sev_mask) == 0u)) {
            continue;
        }
        if ((uint16_t) (pos + 5u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = recs[i].severity;
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 3u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 4u] = recs[i].status;
        pos = (uint16_t) (pos + 5u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportWWHOBDDTCWithPermanentStatus (0x55). Request: SID, sub, FGID.
 * Permanent = confirmed DTCs in the functional group. */
static int uds_internal_dtc_wwhobd_permanent(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                             uint16_t len, bool suppress_pos_resp)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t fgid = data[2];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = fgid;
    tx[3] = ctx->config->dtc_status_availability_mask;
    tx[4] = ctx->config->dtc_format_id;

    uint16_t pos = 5u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].functional_group != fgid) {
            continue;
        }
        if ((recs[i].status & UDS_DTC_STATUS_CONFIRMED) == 0u) {
            continue;
        }
        if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 3u] = recs[i].status;
        pos = (uint16_t) (pos + 4u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportFirstTestFailedDTC (0x0B), reportFirstConfirmedDTC (0x0C),
 * reportMostRecentTestFailedDTC (0x0D), reportMostRecentConfirmedDTC (0x0E).
 * Selects one DTC by occurrence order from fn_dtc_list (which the application
 * returns oldest-first). Request: SID, sub. */
static int uds_internal_dtc_first_or_recent(uds_ctx_t *ctx, uint8_t sub, bool suppress_pos_resp)
{
    uint8_t want = ((sub == 0x0Bu) || (sub == 0x0Du)) ? (uint8_t) UDS_DTC_STATUS_TEST_FAILED
                                                      : (uint8_t) UDS_DTC_STATUS_CONFIRMED;
    bool most_recent = (sub == 0x0Du) || (sub == 0x0Eu);

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    int sel = -1;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if ((recs[i].status & want) != 0u) {
            sel = (int) i;
            if (!most_recent) {
                break;
            }
        }
    }

    uint16_t pos = 3u;
    if (sel >= 0) {
        if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        const uds_dtc_record_t *r = &recs[sel];
        tx[pos] = (uint8_t) ((r->dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((r->dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (r->dtc & 0xFFu);
        tx[pos + 3u] = r->status;
        pos = (uint16_t) (pos + 4u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportDTCWithPermanentStatus (0x15). Permanent DTCs are reported as
 * the confirmed set (the library's documented definition; an application that
 * distinguishes true permanent DTCs can filter them in fn_dtc_list). Request: SID, sub. */
static int uds_internal_dtc_permanent(uds_ctx_t *ctx, uint8_t sub, bool suppress_pos_resp)
{
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if ((recs[i].status & UDS_DTC_STATUS_CONFIRMED) == 0u) {
            continue;
        }
        if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 3u] = recs[i].status;
        pos = (uint16_t) (pos + 4u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/** Max snapshot record numbers gathered per DTC for 0x03 (stack-bounded). */
#ifndef UDS_DTC_SNAPSHOT_ID_BATCH
#define UDS_DTC_SNAPSHOT_ID_BATCH 16u
#endif

/* Map a memory-scoped sub-function to the region fn_dtc_list_mem/fn_dtc_extdata_mem
 * are asked to serve. */
static uds_dtc_memory_t uds_internal_dtc_memory_of(uint8_t sub)
{
    uds_dtc_memory_t memory;

    if ((sub == 0x17u) || (sub == 0x18u) || (sub == 0x19u)) {
        memory = UDS_DTC_MEM_USER_DEFINED;
    }
    else if ((sub == 0x12u) || (sub == 0x13u)) {
        memory = UDS_DTC_MEM_EMISSIONS_OBD;
    }
    else { /* 0x0F, 0x10, 0x11 */
        memory = UDS_DTC_MEM_MIRROR;
    }
    return memory;
}

/* Format reportDTCSnapshotIdentification (0x03). Request: SID, sub.
 * Response: [DTC(3) | snapshotRecordNumber(1)] per stored snapshot. The DTCs
 * come from fn_dtc_list, their record numbers from fn_dtc_snapshot_ids. */
static int uds_internal_dtc_snapshot_ids(uds_ctx_t *ctx, uint8_t sub, bool suppress_pos_resp)
{
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;

    uint16_t pos = 2u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        uint8_t nums[UDS_DTC_SNAPSHOT_ID_BATCH];
        int n = ctx->config->fn_dtc_snapshot_ids(ctx, recs[i].dtc, nums,
                                                 (uint16_t) UDS_DTC_SNAPSHOT_ID_BATCH);
        if (n < 0) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) n);
        }
        if ((uint16_t) n > (uint16_t) UDS_DTC_SNAPSHOT_ID_BATCH) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        for (uint16_t j = 0u; j < (uint16_t) n; j++) {
            if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
                return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
            }
            tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
            tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
            tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
            tx[pos + 3u] = nums[j];
            pos = (uint16_t) (pos + 4u);
        }
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportDTCStoredDataByRecordNumber (0x05) and
 * reportDTCExtDataRecordByRecordNumber (0x16). Request: SID, sub, recordNumber.
 * The library echoes the record number; the application writes the
 * [DTCAndStatusRecord | data] payload that follows. */
static int uds_internal_dtc_by_record_num(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                          uint16_t len, bool suppress_pos_resp)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t record_num = data[2];

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 3u);

    int written;
    if (sub == 0x05u) {
        written = ctx->config->fn_dtc_stored_data(ctx, record_num, &tx[3], max_payload);
    }
    else {
        written = ctx->config->fn_dtc_extdata_by_record(ctx, record_num, &tx[3], max_payload);
    }

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) written);
    }
    if (written > (int) max_payload) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }

    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = record_num;
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + 3u));
}

/* Format the memory-scoped status-mask sub-functions:
 * reportMirrorMemoryDTCByStatusMask (0x0F),
 * reportNumberOfMirrorMemoryDTCByStatusMask (0x11),
 * reportNumberOfEmissionsOBDDTCByStatusMask (0x12),
 * reportEmissionsOBDDTCByStatusMask (0x13) and
 * reportUserDefMemoryDTCByStatusMask (0x17). Records come from fn_dtc_list_mem;
 * the library owns the ISO 14229-1 wire layout. */
static int uds_internal_dtc_mem_by_status(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                          uint16_t len, bool suppress_pos_resp)
{
    /* 0x17 carries a MemorySelection byte after the status mask. */
    bool user_def = (sub == 0x17u);
    if (len < (user_def ? 4u : 3u)) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    uint8_t status_mask = data[2];
    uint8_t mem_selection = user_def ? data[3] : 0u;
    uds_dtc_memory_t memory = uds_internal_dtc_memory_of(sub);
    bool count_only = (sub == 0x11u) || (sub == 0x12u);

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;

    /* 0x17 echoes MemorySelection ahead of the availability mask. */
    uint16_t hdr = 3u;
    if (user_def) {
        tx[2] = mem_selection;
        tx[3] = ctx->config->dtc_status_availability_mask;
        hdr = 4u;
    }
    else {
        tx[2] = ctx->config->dtc_status_availability_mask;
    }

    if (count_only) {
        int count = ctx->config->fn_dtc_list_mem(ctx, memory, mem_selection, status_mask, NULL, 0u);
        if (count < 0) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) count);
        }
        if (suppress_pos_resp) {
            return UDS_OK;
        }
        tx[hdr] = ctx->config->dtc_format_id;
        tx[hdr + 1u] = (uint8_t) (((uint32_t) count >> 8) & 0xFFu);
        tx[hdr + 2u] = (uint8_t) ((uint32_t) count & 0xFFu);
        return uds_send_response(ctx, (uint16_t) (hdr + 3u));
    }

    /* 0x0F / 0x13 / 0x17: emit [DTC(3) | status(1)] per matching record. */
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    uint16_t room = (uint16_t) ((ctx->config->tx_buffer_size - hdr) / 4u);
    uint16_t cap = (room < UDS_DTC_LIST_BATCH) ? room : (uint16_t) UDS_DTC_LIST_BATCH;

    int total = ctx->config->fn_dtc_list_mem(ctx, memory, mem_selection, status_mask, recs, cap);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > cap) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }

    uint16_t pos = hdr;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 3u] = recs[i].status;
        pos = (uint16_t) (pos + 4u);
    }
    return uds_send_response(ctx, pos);
}

/* Format the memory-scoped record-by-DTC sub-functions:
 * reportMirrorMemoryDTCExtDataRecordByDTCNumber (0x10),
 * reportUserDefMemoryDTCSnapshotRecordByDTCNumber (0x18) and
 * reportUserDefMemoryDTCExtDataRecordByDTCNumber (0x19).
 * Request: SID, sub, DTC(3), recordNumber [, MemorySelection for 0x18/0x19].
 * The library validates and frames; the application writes statusOfDTC plus the
 * record payload. */
static int uds_internal_dtc_mem_record(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                       uint16_t len, bool suppress_pos_resp)
{
    /* 0x18/0x19 append a MemorySelection byte after the record number. */
    bool user_def = (sub == 0x18u) || (sub == 0x19u);
    if (len < (user_def ? 7u : 6u)) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }

    uint32_t dtc = (uint32_t) ((uint32_t) data[2] << 16) | (uint32_t) ((uint32_t) data[3] << 8) |
                   (uint32_t) data[4];
    uint8_t record_num = data[5];
    uint8_t mem_selection = user_def ? data[6] : 0u;

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    /* 0x18/0x19 echo MemorySelection ahead of the DTC. */
    uint16_t hdr = 5u;
    if (user_def) {
        hdr = 6u;
    }
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - hdr);

    int written;
    if (sub == 0x18u) {
        written = ctx->config->fn_dtc_snapshot_mem(ctx, mem_selection, dtc, record_num, &tx[hdr],
                                                   max_payload);
    }
    else {
        written =
            ctx->config->fn_dtc_extdata_mem(ctx, uds_internal_dtc_memory_of(sub), mem_selection,
                                            dtc, record_num, &tx[hdr], max_payload);
    }

    if (written < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) written);
    }
    if (written > (int) max_payload) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }

    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    uint16_t pos = 2u;
    if (user_def) {
        tx[pos] = mem_selection;
        pos++;
    }
    tx[pos] = data[2];
    tx[pos + 1u] = data[3];
    tx[pos + 2u] = data[4];
    return uds_send_response(ctx, (uint16_t) ((uint16_t) written + hdr));
}

void uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                       uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);
    bool suppress_pos_resp = (bool) ((sub_raw & 0x80u) != 0u);

    /* C-10: DTC Status Mask Requirement */
    bool req_mask = (sub == 0x01u || sub == 0x02u || sub == 0x07u || sub == 0x08u || sub == 0x0Fu ||
                     sub == 0x11u || sub == 0x12u || sub == 0x13u || sub == 0x17u);

    if (req_mask && len < 3u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    /* The static helpers below call uds_send_response/uds_send_nrc directly (legacy
     * compat shim path). Signal NONE so execute_handler does not attempt a second
     * emit after control returns here. */

    /* Structured subfunctions: library formats the ISO wire layout when the
     * application provides fn_dtc_list. Falls through to the raw fn_dtc_read
     * path below when fn_dtc_list is NULL (back-compat). */
    if (((sub == 0x01u) || (sub == 0x02u) || (sub == 0x0Au)) &&
        (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_by_status(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x04u) && (ctx->config->fn_dtc_snapshot != NULL)) {
        (void) uds_internal_dtc_record(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x06u) && (ctx->config->fn_dtc_extdata != NULL)) {
        (void) uds_internal_dtc_record(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if (((sub == 0x07u) || (sub == 0x08u)) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_by_severity(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x09u) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_severity_info(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x14u) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_fault_counter(ctx, sub, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x42u) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_wwhobd(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x55u) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_wwhobd_permanent(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if (((sub == 0x0Bu) || (sub == 0x0Cu) || (sub == 0x0Du) || (sub == 0x0Eu)) &&
        (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_first_or_recent(ctx, sub, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x15u) && (ctx->config->fn_dtc_list != NULL)) {
        (void) uds_internal_dtc_permanent(ctx, sub, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x03u) && (ctx->config->fn_dtc_list != NULL) &&
        (ctx->config->fn_dtc_snapshot_ids != NULL)) {
        (void) uds_internal_dtc_snapshot_ids(ctx, sub, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x05u) && (ctx->config->fn_dtc_stored_data != NULL)) {
        (void) uds_internal_dtc_by_record_num(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x16u) && (ctx->config->fn_dtc_extdata_by_record != NULL)) {
        (void) uds_internal_dtc_by_record_num(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if (((sub == 0x0Fu) || (sub == 0x11u) || (sub == 0x12u) || (sub == 0x13u) || (sub == 0x17u)) &&
        (ctx->config->fn_dtc_list_mem != NULL)) {
        (void) uds_internal_dtc_mem_by_status(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if ((sub == 0x18u) && (ctx->config->fn_dtc_snapshot_mem != NULL)) {
        (void) uds_internal_dtc_mem_record(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if (((sub == 0x10u) || (sub == 0x19u)) && (ctx->config->fn_dtc_extdata_mem != NULL)) {
        (void) uds_internal_dtc_mem_record(ctx, sub, data, len, suppress_pos_resp);
        uds_none(out);
        return;
    }

    if (!ctx->config->fn_dtc_read) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = true;
    }

    uint8_t *out_payload = &ctx->config->tx_buffer[2];
    uint16_t max_payload = (uint16_t) (ctx->config->tx_buffer_size - 2u);

    /* Pass the sub-function and the full request so the application can read the
     * status/severity mask, DTC, record number, or memory selection it needs. */
    int written = ctx->config->fn_dtc_read(ctx, sub, data, len, out_payload, max_payload);
    if (written < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) written);
        return;
    }

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    uds_ok(out, (uint16_t) ((uint16_t) written + 2u));
}

void uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                             uds_result_t *out)
{
    if (len < 2u) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    uint8_t sub_raw = data[1];
    uint8_t sub = (uint8_t) (sub_raw & 0x7Fu);

    if ((sub != 0x01u) && (sub != 0x02u)) { /* ON / OFF */
        uds_nrc(out, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    /* Optional application hook: take a real side effect (e.g. freeze DTC
     * storage for the duration of a reprogramming sequence). A negative return
     * is surfaced as the corresponding NRC and the setting is left unchanged. */
    if (ctx->config->fn_control_dtc_setting != NULL) {
        int ret = ctx->config->fn_control_dtc_setting(ctx, sub);
        if (ret != UDS_OK) {
            uds_nrc(out, (uint8_t) - (int32_t) ret);
            return;
        }
    }

    /* Track the setting so a return to the default session can restore it
     * (ISO 14229-1: DTC setting reverts to "on" on default-session entry).
     * sub 0x02 = off, 0x01 = on. */
    ctx->session.dtc_setting_disabled = (sub == 0x02u) ? 0x01u : 0x00u;

    ctx->config->tx_buffer[0] = (uint8_t) (UDS_SID_CONTROL_DTC_SETTING + UDS_RESPONSE_OFFSET);
    ctx->config->tx_buffer[1] = sub;
    uds_ok(out, 2u);
}
