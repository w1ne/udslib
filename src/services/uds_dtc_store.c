/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "uds/uds_dtc_store.h"

#include "uds_internal.h"
#include "uds/uds_dtc.h"

void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing, uint16_t capacity,
                        uint8_t aging_threshold)
{
    s->entries = backing;
    s->capacity = capacity;
    s->count = 0u;
    s->aging_threshold = aging_threshold;
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
        if (r->fault_detection_counter < 0x7F) {
            r->fault_detection_counter++;
        }
        r->status |=
            (uint8_t) (UDS_DTC_STATUS_TEST_FAILED | UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE |
                       UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR);
        if ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u) {
            r->aging_counter = 0u;
        }
        if (r->fault_detection_counter >= 0x7F) {
            r->status |= (uint8_t) (UDS_DTC_STATUS_CONFIRMED | UDS_DTC_STATUS_PENDING);
            r->aging_counter = 0u;
        }
    }
    else {
        if (r->fault_detection_counter > -128) {
            r->fault_detection_counter--;
        }
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED;
    }
}

void uds_dtc_store_operation_cycle(uds_dtc_store_t *s)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        uds_dtc_record_t *r = &s->entries[i];
        bool failed_this_cycle = (r->status & UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE) != 0u;
        /* Reset per-cycle bits and fault-detection counter for the new cycle. */
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE;
        r->fault_detection_counter = 0;
        /* Age a confirmed DTC only on a cycle where it did not fail; self-heal
         * once it has survived aging_threshold clean cycles. */
        if (!failed_this_cycle && ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u)) {
            if (r->aging_counter < 0xFFu) {
                r->aging_counter++;
            }
            if (r->aging_counter >= s->aging_threshold) {
                r->status = 0u;
                r->aging_counter = 0u;
            }
        }
    }
}

void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if ((group == 0xFFFFFFu) || (s->entries[i].dtc == group)) {
            s->entries[i].status = 0u;
            s->entries[i].fault_detection_counter = 0;
            s->entries[i].aging_counter = 0u;
        }
    }
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

int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return 0;
    }
    if (max_len < 4u) {
        return -(int) UDS_NRC_RESPONSE_TOO_LONG;
    }
    out_buf[0] = r->status;
    out_buf[1] = record_num;
    out_buf[2] = r->aging_counter;
    out_buf[3] = (uint8_t) r->fault_detection_counter;
    return 4;
}

int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_store_clear(s, group);
    return UDS_OK;
}
