/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "uds/uds_dtc_store.h"

#include <string.h>

#include "uds_internal.h"
#include "uds/uds_dtc.h"

static void uds_dtc_sync_ageing(uds_dtc_record_t *r, uint8_t value)
{
    r->aging_counter = value;
    r->extended.ageing_counter = value;
}

static void uds_dtc_zero_snapshot(uds_dtc_record_t *r)
{
    r->snapshot_valid = 0u;
    memset(&r->snapshot, 0, sizeof(r->snapshot));
}

/* First confirmation, and each later operation cycle that fails again. */
static void uds_dtc_note_occurrence(uds_dtc_store_t *s, uds_dtc_record_t *r)
{
    if (r->extended.fault_occur_counter < 0xFFu) {
        r->extended.fault_occur_counter++;
    }
    r->extended.fault_pending_counter = 1u;
    uds_dtc_sync_ageing(r, 0u);
    if (s->environment_set) {
        r->snapshot = s->environment;
        r->snapshot_valid = 1u;
    }
}

void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing, uint16_t capacity,
                        uint8_t aging_threshold)
{
    s->entries = backing;
    s->capacity = capacity;
    s->count = 0u;
    s->aging_threshold = aging_threshold;
    s->environment_set = false;
    memset(&s->environment, 0, sizeof(s->environment));
}

int uds_dtc_store_register(uds_dtc_store_t *s, uint32_t dtc, uint8_t severity,
                           uint8_t functional_unit, uint8_t functional_group)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if (s->entries[i].dtc == dtc) {
            s->entries[i].severity = severity;
            s->entries[i].functional_unit = functional_unit;
            s->entries[i].functional_group = functional_group;
            return (int) i;
        }
    }
    if (s->count >= s->capacity) {
        return -1;
    }
    uds_dtc_record_t *r = &s->entries[s->count];
    r->dtc = dtc;
    r->status = 0u;
    r->severity = severity;
    r->functional_unit = functional_unit;
    r->fault_detection_counter = 0;
    r->aging_counter = 0u;
    r->functional_group = functional_group;
    r->extended.fault_occur_counter = 0u;
    r->extended.fault_pending_counter = 0u;
    r->extended.aged_counter = 0u;
    r->extended.ageing_counter = 0u;
    uds_dtc_zero_snapshot(r);
    s->count++;
    return (int) (s->count - 1u);
}

uds_dtc_record_t *uds_dtc_store_get(uds_dtc_store_t *s, uint32_t dtc)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if (s->entries[i].dtc == dtc) {
            return &s->entries[i];
        }
    }
    return NULL;
}

void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed)
{
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return;
    }
    if (failed) {
        bool failed_this_cycle = (r->status & UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE) != 0u;
        bool was_confirmed = (r->status & UDS_DTC_STATUS_CONFIRMED) != 0u;
        if (r->fault_detection_counter < 0x7F) {
            r->fault_detection_counter++;
        }
        r->status |=
            (uint8_t) (UDS_DTC_STATUS_TEST_FAILED | UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE |
                       UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR);
        if ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u) {
            uds_dtc_sync_ageing(r, 0u);
        }
        if (r->fault_detection_counter >= 0x7F) {
            r->status |= (uint8_t) (UDS_DTC_STATUS_CONFIRMED | UDS_DTC_STATUS_PENDING);
            uds_dtc_sync_ageing(r, 0u);
        }
        /* Count a confirmed occurrence once per operation cycle, and freeze
         * the environment published by uds_dtc_store_set_environment(). */
        bool now_confirmed = (r->status & UDS_DTC_STATUS_CONFIRMED) != 0u;
        if (now_confirmed && (!was_confirmed || !failed_this_cycle)) {
            uds_dtc_note_occurrence(s, r);
        }
    }
    else {
        if (r->fault_detection_counter > -128) {
            r->fault_detection_counter--;
        }
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED;
        r->extended.fault_pending_counter = 0u;
    }
}

void uds_dtc_store_set_environment(uds_dtc_store_t *s, const uds_dtc_snapshot_t *env)
{
    if ((s == NULL) || (env == NULL)) {
        return;
    }
    s->environment = *env;
    s->environment_set = true;
}

void uds_dtc_store_operation_cycle(uds_dtc_store_t *s)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        uds_dtc_record_t *r = &s->entries[i];
        bool failed_this_cycle = (r->status & UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE) != 0u;
        /* Reset per-cycle bits and fault-detection counter for the new cycle. */
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE;
        r->fault_detection_counter = 0;
        if (!failed_this_cycle) {
            r->extended.fault_pending_counter = 0u;
        }
        /* Age a confirmed DTC only on a cycle where it did not fail; self-heal
         * once it has survived aging_threshold clean cycles. */
        if (!failed_this_cycle && ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u)) {
            if (r->aging_counter < 0xFFu) {
                uds_dtc_sync_ageing(r, (uint8_t) (r->aging_counter + 1u));
            }
            if (r->aging_counter >= s->aging_threshold) {
                if (r->extended.aged_counter < 0xFFu) {
                    r->extended.aged_counter++;
                }
                r->status = 0u;
                uds_dtc_sync_ageing(r, 0u);
                uds_dtc_zero_snapshot(r);
            }
        }
    }
}

void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if ((group == 0xFFFFFFu) || (s->entries[i].dtc == group)) {
            uds_dtc_record_t *r = &s->entries[i];
            r->status = 0u;
            r->fault_detection_counter = 0;
            uds_dtc_sync_ageing(r, 0u);
            r->extended.fault_occur_counter = 0u;
            r->extended.fault_pending_counter = 0u;
            r->extended.aged_counter = 0u;
            uds_dtc_zero_snapshot(r);
        }
    }
}

#define UDS_DTC_STORE_BLOB_VERSION 0x02u
#define UDS_DTC_STORE_BLOB_VERSION_V1 0x01u
#define UDS_DTC_STORE_BLOB_HDR 3u
#define UDS_DTC_STORE_BLOB_REC 18u
#define UDS_DTC_STORE_BLOB_REC_V1 6u

static void uds_dtc_store_write_rec(uint8_t *p, const uds_dtc_record_t *r)
{
    uint32_t dtc = r->dtc & 0x00FFFFFFu;
    p[0] = (uint8_t) ((dtc >> 16) & 0xFFu);
    p[1] = (uint8_t) ((dtc >> 8) & 0xFFu);
    p[2] = (uint8_t) (dtc & 0xFFu);
    p[3] = r->status;
    p[4] = (uint8_t) r->fault_detection_counter;
    p[5] = r->aging_counter;
    p[6] = r->extended.fault_occur_counter;
    p[7] = r->extended.fault_pending_counter;
    p[8] = r->extended.aged_counter;
    p[9] = r->snapshot_valid;
    p[10] = r->snapshot.voltage;
    p[11] = r->snapshot.power_mode;
    p[12] = r->snapshot.time.second;
    p[13] = r->snapshot.time.minute;
    p[14] = r->snapshot.time.hour;
    p[15] = r->snapshot.time.day;
    p[16] = r->snapshot.time.month;
    p[17] = r->snapshot.time.year;
}

static void uds_dtc_store_read_common(uds_dtc_record_t *r, const uint8_t *p)
{
    uint8_t raw_fdc = p[4];
    r->status = p[3];
    /* int8_t is two's complement on the wire: 0x00..0x7F positive, 0x80..0xFF negative. */
    r->fault_detection_counter =
        (raw_fdc < 0x80u) ? (int8_t) raw_fdc : (int8_t) ((int16_t) raw_fdc - 256);
    uds_dtc_sync_ageing(r, p[5]);
}

static uds_dtc_record_t *uds_dtc_store_find24(uds_dtc_store_t *s, uint32_t dtc24)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if ((s->entries[i].dtc & 0x00FFFFFFu) == dtc24) {
            return &s->entries[i];
        }
    }
    return NULL;
}

int uds_dtc_store_serialize(const uds_dtc_store_t *s, uint8_t *buf, uint16_t max)
{
    if ((s == NULL) || (buf == NULL) || ((s->count > 0u) && (s->entries == NULL))) {
        return UDS_ERR_INVALID_ARG;
    }

    uint32_t need = (uint32_t) UDS_DTC_STORE_BLOB_HDR +
                    ((uint32_t) s->count * (uint32_t) UDS_DTC_STORE_BLOB_REC);
    if (need > (uint32_t) max) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    buf[0] = UDS_DTC_STORE_BLOB_VERSION;
    buf[1] = (uint8_t) ((s->count >> 8) & 0xFFu);
    buf[2] = (uint8_t) (s->count & 0xFFu);

    uint16_t pos = (uint16_t) UDS_DTC_STORE_BLOB_HDR;
    for (uint16_t i = 0u; i < s->count; i++) {
        uds_dtc_store_write_rec(&buf[pos], &s->entries[i]);
        pos = (uint16_t) (pos + UDS_DTC_STORE_BLOB_REC);
    }
    return (int) pos;
}

int uds_dtc_store_deserialize(uds_dtc_store_t *s, const uint8_t *buf, uint16_t len)
{
    if ((s == NULL) || (buf == NULL) || ((s->count > 0u) && (s->entries == NULL))) {
        return UDS_ERR_INVALID_ARG;
    }
    if ((len < (uint16_t) UDS_DTC_STORE_BLOB_HDR) ||
        ((buf[0] != UDS_DTC_STORE_BLOB_VERSION) && (buf[0] != UDS_DTC_STORE_BLOB_VERSION_V1))) {
        return UDS_ERR_INVALID_ARG;
    }

    uint8_t rec_len = (buf[0] == UDS_DTC_STORE_BLOB_VERSION_V1) ? (uint8_t) UDS_DTC_STORE_BLOB_REC_V1
                                                               : (uint8_t) UDS_DTC_STORE_BLOB_REC;
    uint16_t count = (uint16_t) (((uint16_t) buf[1] << 8) | (uint16_t) buf[2]);
    uint32_t need = (uint32_t) UDS_DTC_STORE_BLOB_HDR + ((uint32_t) count * (uint32_t) rec_len);
    /* Reject a short blob before any record is written. Extra trailing bytes
     * (a flash page larger than the blob) are ignored. */
    if ((uint32_t) len < need) {
        return UDS_ERR_INVALID_ARG;
    }

    uint16_t restored = 0u;
    uint16_t pos = (uint16_t) UDS_DTC_STORE_BLOB_HDR;
    for (uint16_t i = 0u; i < count; i++) {
        uint32_t dtc = ((uint32_t) buf[pos] << 16) | ((uint32_t) buf[pos + 1u] << 8) |
                       (uint32_t) buf[pos + 2u];
        uds_dtc_record_t *r = uds_dtc_store_find24(s, dtc);
        if (r != NULL) {
            uds_dtc_store_read_common(r, &buf[pos]);
            if (buf[0] == UDS_DTC_STORE_BLOB_VERSION) {
                r->extended.fault_occur_counter = buf[pos + 6u];
                r->extended.fault_pending_counter = buf[pos + 7u];
                r->extended.aged_counter = buf[pos + 8u];
                r->snapshot_valid = buf[pos + 9u];
                r->snapshot.voltage = buf[pos + 10u];
                r->snapshot.power_mode = buf[pos + 11u];
                r->snapshot.time.second = buf[pos + 12u];
                r->snapshot.time.minute = buf[pos + 13u];
                r->snapshot.time.hour = buf[pos + 14u];
                r->snapshot.time.day = buf[pos + 15u];
                r->snapshot.time.month = buf[pos + 16u];
                r->snapshot.time.year = buf[pos + 17u];
            }
            else {
                /* A v1 blob has no freeze frame. Do not keep a previous one. */
                r->extended.fault_occur_counter = 0u;
                r->extended.fault_pending_counter = 0u;
                r->extended.aged_counter = 0u;
                uds_dtc_zero_snapshot(r);
            }
            restored++;
        }
        pos = (uint16_t) (pos + rec_len);
    }
    return (int) restored;
}

int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                          uint16_t max)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < s->count; i++) {
        bool match = (status_mask == 0u) || ((s->entries[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = s->entries[i];
            }
            n++;
        }
    }
    return (int) n;
}

static bool uds_dtc_known_record(uint8_t record_num)
{
    return (record_num == 0x01u) || (record_num == 0xFFu);
}

int uds_dtc_store_snapshot_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                              uint8_t *out_buf, uint16_t max_len)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (!uds_dtc_known_record(record_num)) {
        return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    /* No stored freeze frame: DTC + statusOfDTC, and no snapshot records. */
    if (r->snapshot_valid == 0u) {
        if (max_len < 1u) {
            return -(int) UDS_NRC_RESPONSE_TOO_LONG;
        }
        out_buf[0] = r->status;
        return 1;
    }
    if (max_len < 15u) {
        return -(int) UDS_NRC_RESPONSE_TOO_LONG;
    }
    out_buf[0] = r->status;
    out_buf[1] = 0x01u;
    out_buf[2] = 0x02u;
    out_buf[3] = 0x10u; /* DID 0x1001: year, month, day, hour, minute, second */
    out_buf[4] = 0x01u;
    out_buf[5] = r->snapshot.time.year;
    out_buf[6] = r->snapshot.time.month;
    out_buf[7] = r->snapshot.time.day;
    out_buf[8] = r->snapshot.time.hour;
    out_buf[9] = r->snapshot.time.minute;
    out_buf[10] = r->snapshot.time.second;
    out_buf[11] = 0x10u; /* DID 0x1002: voltage, power mode */
    out_buf[12] = 0x02u;
    out_buf[13] = r->snapshot.voltage;
    out_buf[14] = r->snapshot.power_mode;
    return 15;
}

int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (!uds_dtc_known_record(record_num)) {
        return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (max_len < 6u) {
        return -(int) UDS_NRC_RESPONSE_TOO_LONG;
    }
    out_buf[0] = r->status;
    out_buf[1] = 0x01u;
    out_buf[2] = r->extended.fault_occur_counter;
    out_buf[3] = r->extended.fault_pending_counter;
    out_buf[4] = r->extended.aged_counter;
    out_buf[5] = r->extended.ageing_counter;
    return 6;
}

int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_store_clear(s, group);
    return UDS_OK;
}
