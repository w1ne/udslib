/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "uds/uds_core.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"
#include "uds_internal.h"

static void test_store_register_and_get(void **state)
{
    (void) state;
    uds_dtc_record_t backing[4];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 4u, 40u);

    assert_int_equal(uds_dtc_store_register(&s, 0x012345u, 0x80u, 0x10u, UDS_DTC_FGID_EMISSIONS),
                     0);
    assert_int_equal(s.count, 1u);

    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x012345u);
    assert_non_null(r);
    assert_int_equal(r->severity, 0x80u);
    assert_int_equal(r->functional_group, UDS_DTC_FGID_EMISSIONS);
    assert_null(uds_dtc_store_get(&s, 0x999999u));
}

static void test_store_register_full(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 40u);
    assert_int_equal(uds_dtc_store_register(&s, 0x111111u, 0, 0, 0), 0);
    assert_int_equal(uds_dtc_store_register(&s, 0x222222u, 0, 0, 0), -1);
}

static void test_store_fault_counter_confirms(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    uds_dtc_store_report_test(&s, 0x111111u, true);
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_int_equal(r->fault_detection_counter, 1);
    assert_true((r->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);
    assert_false((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);

    for (int i = 0; i < 200; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, true);
    }
    assert_int_equal(r->fault_detection_counter, 0x7F);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
}

static void test_store_aging_self_heal(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    /* Drive to confirmed. */
    for (int i = 0; i < 200; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, true);
    }
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);

    /* The drive-to-confirmed loop leaves TEST_FAILED_THIS_OP_CYCLE set, so
     * cycle 1 is a "failed" cycle and does NOT advance aging.  Three
     * subsequent clean cycles are required to reach the threshold (3). */
    uds_dtc_store_operation_cycle(&s); /* cycle 1: consumes failed flag, aging stays 0 */
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
    uds_dtc_store_operation_cycle(&s); /* cycle 2: aging_counter = 1 */
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
    uds_dtc_store_operation_cycle(&s); /* cycle 3: aging_counter = 2 */
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
    uds_dtc_store_operation_cycle(&s); /* cycle 4: aging_counter = 3 >= threshold -> self-heal */
    assert_int_equal(r->status, 0u);
}

/* Runtime state survives a reset only through a caller-owned buffer.
 * Configuration (severity, functional unit, group, aging threshold) is
 * registered again at boot and must not be taken from the blob. */
static void test_store_persist_round_trip(void **state)
{
    (void) state;
    uds_dtc_record_t src_mem[4];
    uds_dtc_store_t src;
    uds_dtc_store_init(&src, src_mem, 4u, 40u);
    uds_dtc_store_register(&src, 0x012345u, 0x80u, 0x10u, UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_register(&src, 0xDCBA98u, 0x40u, 0x20u, UDS_DTC_FGID_SAFETY);

    for (int i = 0; i < 200; i++) {
        uds_dtc_store_report_test(&src, 0xDCBA98u, true);
    }
    uds_dtc_store_operation_cycle(&src); /* failed-this-cycle consumed; aging stays 0 */
    uds_dtc_store_operation_cycle(&src); /* aging_counter = 1, still confirmed */
    /* An operation cycle clears the fault-detection counter, so the negative
     * value has to be reported after the cycle we want to persist. */
    uds_dtc_store_report_test(&src, 0x012345u, false);

    uds_dtc_record_t *passed = uds_dtc_store_get(&src, 0x012345u);
    uds_dtc_record_t *confirmed = uds_dtc_store_get(&src, 0xDCBA98u);
    assert_int_equal(passed->fault_detection_counter, -1);
    assert_int_equal(confirmed->aging_counter, 1u);

    uint8_t blob[64];
    int n = uds_dtc_store_serialize(&src, blob, (uint16_t) sizeof(blob));
    assert_int_equal(n, 3 + (18 * 2));
    assert_int_equal(blob[0], 0x02);

    uds_dtc_record_t dst_mem[4];
    uds_dtc_store_t dst;
    uds_dtc_store_init(&dst, dst_mem, 4u, 7u);
    /* Opposite registration order, and different catalog metadata. */
    uds_dtc_store_register(&dst, 0xDCBA98u, 0x20u, 0x55u, UDS_DTC_FGID_VOBD);
    uds_dtc_store_register(&dst, 0x012345u, 0x20u, 0x66u, UDS_DTC_FGID_VOBD);

    assert_int_equal(uds_dtc_store_deserialize(&dst, blob, (uint16_t) n), 2);

    uds_dtc_record_t *a = uds_dtc_store_get(&dst, 0x012345u);
    assert_int_equal(a->status, passed->status);
    assert_int_equal(a->fault_detection_counter, -1);
    assert_int_equal(a->aging_counter, passed->aging_counter);
    assert_int_equal(a->severity, 0x20u);
    assert_int_equal(a->functional_unit, 0x66u);
    assert_int_equal(a->functional_group, UDS_DTC_FGID_VOBD);

    uds_dtc_record_t *b = uds_dtc_store_get(&dst, 0xDCBA98u);
    assert_int_equal(b->status, confirmed->status);
    assert_int_equal(b->fault_detection_counter, confirmed->fault_detection_counter);
    assert_int_equal(b->aging_counter, 1u);
    assert_int_equal(b->severity, 0x20u);
    assert_int_equal(dst.aging_threshold, 7u);
}

static void test_store_persist_rejects_bad_input(void **state)
{
    (void) state;
    uds_dtc_record_t mem[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, mem, 2u, 40u);
    uds_dtc_store_register(&s, 0x111111u, 0x80u, 0x01u, 0u);
    uds_dtc_store_report_test(&s, 0x111111u, true);

    uint8_t tiny[2];
    assert_int_equal(uds_dtc_store_serialize(&s, tiny, (uint16_t) sizeof(tiny)),
                     UDS_ERR_BUFFER_TOO_SMALL);
    assert_int_equal(uds_dtc_store_serialize(NULL, tiny, (uint16_t) sizeof(tiny)),
                     UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_dtc_store_serialize(&s, NULL, 8u), UDS_ERR_INVALID_ARG);

    uint8_t blob[40];
    int n = uds_dtc_store_serialize(&s, blob, (uint16_t) sizeof(blob));
    assert_true(n > 3);

    uds_dtc_record_t mem2[2];
    uds_dtc_store_t dst;
    uds_dtc_store_init(&dst, mem2, 2u, 40u);
    uds_dtc_store_register(&dst, 0x111111u, 0x80u, 0x01u, 0u);

    assert_int_equal(uds_dtc_store_deserialize(NULL, blob, (uint16_t) n), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_dtc_store_deserialize(&dst, NULL, (uint16_t) n), UDS_ERR_INVALID_ARG);

    uint8_t bad[40];
    memcpy(bad, blob, (size_t) n);
    bad[0] = 0x03u;
    assert_int_equal(uds_dtc_store_deserialize(&dst, bad, (uint16_t) n), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_dtc_store_get(&dst, 0x111111u)->status, 0u);

    /* Truncated record: do not apply a partial DTC. */
    assert_int_equal(uds_dtc_store_deserialize(&dst, blob, 4u), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_dtc_store_get(&dst, 0x111111u)->status, 0u);

    /* Header claims two records but only one is present. */
    memcpy(bad, blob, (size_t) n);
    bad[1] = 0x00u;
    bad[2] = 0x02u;
    assert_int_equal(uds_dtc_store_deserialize(&dst, bad, (uint16_t) n), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_dtc_store_get(&dst, 0x111111u)->status, 0u);
}

static void test_store_persist_skips_unknown_and_keeps_absent(void **state)
{
    (void) state;
    uds_dtc_record_t src_mem[2];
    uds_dtc_store_t src;
    uds_dtc_store_init(&src, src_mem, 2u, 40u);
    uds_dtc_store_register(&src, 0x111111u, 0x80u, 0x01u, 0u);
    uds_dtc_store_report_test(&src, 0x111111u, true);

    uint8_t blob[40];
    int n = uds_dtc_store_serialize(&src, blob, (uint16_t) sizeof(blob));

    uds_dtc_record_t only_mem[2];
    uds_dtc_store_t only;
    uds_dtc_store_init(&only, only_mem, 2u, 40u);
    uds_dtc_store_register(&only, 0x222222u, 0x11u, 0x22u, 0u);
    assert_int_equal(uds_dtc_store_deserialize(&only, blob, (uint16_t) n), 0);
    assert_int_equal(uds_dtc_store_get(&only, 0x222222u)->status, 0u);

    uds_dtc_record_t both_mem[2];
    uds_dtc_store_t both;
    uds_dtc_store_init(&both, both_mem, 2u, 40u);
    uds_dtc_store_register(&both, 0x111111u, 0x80u, 0x01u, 0u);
    uds_dtc_store_register(&both, 0x333333u, 0x80u, 0x01u, 0u);
    assert_int_equal(uds_dtc_store_deserialize(&both, blob, (uint16_t) n), 1);
    assert_true((uds_dtc_store_get(&both, 0x111111u)->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);
    assert_int_equal(uds_dtc_store_get(&both, 0x333333u)->status, 0u);
}

static void test_store_persist_empty_and_24bit_dtc(void **state)
{
    (void) state;
    uds_dtc_record_t empty_mem[1];
    uds_dtc_store_t empty;
    uds_dtc_store_init(&empty, empty_mem, 1u, 40u);

    uint8_t blob[40];
    int n = uds_dtc_store_serialize(&empty, blob, (uint16_t) sizeof(blob));
    assert_int_equal(n, 3);
    assert_int_equal(blob[0], 0x02);
    assert_int_equal(blob[1], 0x00);
    assert_int_equal(blob[2], 0x00);

    uds_dtc_store_register(&empty, 0x111111u, 1u, 2u, 3u);
    assert_int_equal(uds_dtc_store_deserialize(&empty, blob, 3u), 0);
    assert_int_equal(uds_dtc_store_get(&empty, 0x111111u)->status, 0u);

    uds_dtc_record_t src_mem[1];
    uds_dtc_store_t src;
    uds_dtc_store_init(&src, src_mem, 1u, 40u);
    uds_dtc_store_register(&src, 0xAB012345u, 0x80u, 0x10u, 0u);
    uds_dtc_store_report_test(&src, 0xAB012345u, true);
    n = uds_dtc_store_serialize(&src, blob, (uint16_t) sizeof(blob));
    assert_int_equal(blob[3], 0x01);
    assert_int_equal(blob[4], 0x23);
    assert_int_equal(blob[5], 0x45);

    uds_dtc_record_t dst_mem[1];
    uds_dtc_store_t dst;
    uds_dtc_store_init(&dst, dst_mem, 1u, 40u);
    uds_dtc_store_register(&dst, 0x012345u, 0x11u, 0x22u, 0u);
    assert_int_equal(uds_dtc_store_deserialize(&dst, blob, (uint16_t) n), 1);
    uds_dtc_record_t *r = uds_dtc_store_get(&dst, 0x012345u);
    assert_true((r->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);
    assert_int_equal(r->dtc, 0x012345u);
    assert_int_equal(r->severity, 0x11u);
}

static void confirm_dtc(uds_dtc_store_t *s, uint32_t dtc)
{
    for (int i = 0; i < 127; i++) {
        uds_dtc_store_report_test(s, dtc, true);
    }
}

static uds_dtc_snapshot_t sample_env(void)
{
    uds_dtc_snapshot_t env;
    env.voltage = 0x8Cu;
    env.power_mode = 0x02u;
    env.time.second = 0x2Au;
    env.time.minute = 0x05u;
    env.time.hour = 0x0Du;
    env.time.day = 0x13u;
    env.time.month = 0x06u;
    env.time.year = 0x19u;
    return env;
}

static int call_snapshot(uds_dtc_store_t *s, uint32_t dtc, uint8_t rec, uint8_t *out, uint16_t max)
{
    uds_config_t cfg;
    uds_ctx_t ctx;
    memset(&cfg, 0, sizeof(cfg));
    memset(&ctx, 0, sizeof(ctx));
    cfg.app_data = s;
    ctx.config = &cfg;
    return uds_dtc_store_snapshot_cb(&ctx, dtc, rec, out, max);
}

static int call_extdata(uds_dtc_store_t *s, uint32_t dtc, uint8_t rec, uint8_t *out, uint16_t max)
{
    uds_config_t cfg;
    uds_ctx_t ctx;
    memset(&cfg, 0, sizeof(cfg));
    memset(&ctx, 0, sizeof(ctx));
    cfg.app_data = s;
    ctx.config = &cfg;
    return uds_dtc_store_extdata_cb(&ctx, dtc, rec, out, max);
}

/* Occurrence counts confirmed cycles, pending tracks the fault, and the
 * freeze frame is the environment at confirmation. */
static void test_store_snapshot_and_extended_on_confirm(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 40u);
    uds_dtc_store_register(&s, 0x012345u, 0x80u, 0x10u, UDS_DTC_FGID_EMISSIONS);

    uds_dtc_snapshot_t env = sample_env();
    uds_dtc_store_set_environment(&s, &env);

    uds_dtc_store_report_test(&s, 0x012345u, true);
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x012345u);
    assert_int_equal(r->extended.fault_occur_counter, 0u);
    assert_int_equal(r->snapshot_valid, 0u);

    confirm_dtc(&s, 0x012345u);
    assert_int_equal(r->extended.fault_occur_counter, 1u);
    assert_int_equal(r->extended.fault_pending_counter, 1u);
    assert_int_equal(r->extended.aged_counter, 0u);
    assert_int_equal(r->extended.ageing_counter, 0u);
    assert_int_equal(r->aging_counter, 0u);
    assert_int_equal(r->snapshot_valid, 1u);
    assert_int_equal(r->snapshot.voltage, 0x8Cu);
    assert_int_equal(r->snapshot.power_mode, 0x02u);
    assert_int_equal(r->snapshot.time.year, 0x19u);
    assert_int_equal(r->snapshot.time.second, 0x2Au);

    /* Further failures in the same operation cycle do not count again. */
    uds_dtc_store_report_test(&s, 0x012345u, true);
    assert_int_equal(r->extended.fault_occur_counter, 1u);

    uds_dtc_store_operation_cycle(&s);
    env.voltage = 0x7Au;
    uds_dtc_store_set_environment(&s, &env);
    uds_dtc_store_report_test(&s, 0x012345u, true);
    assert_int_equal(r->extended.fault_occur_counter, 2u);
    assert_int_equal(r->snapshot.voltage, 0x7Au);

    uds_dtc_store_report_test(&s, 0x012345u, false);
    assert_int_equal(r->extended.fault_pending_counter, 0u);
    assert_int_equal(r->extended.fault_occur_counter, 2u);
    assert_int_equal(r->snapshot_valid, 1u);
}

static void test_store_aged_counter_on_self_heal(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);
    uds_dtc_snapshot_t env = sample_env();
    uds_dtc_store_set_environment(&s, &env);
    confirm_dtc(&s, 0x111111u);

    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    uds_dtc_store_operation_cycle(&s); /* consumes the failed cycle */
    uds_dtc_store_operation_cycle(&s);
    uds_dtc_store_operation_cycle(&s);
    uds_dtc_store_operation_cycle(&s); /* ageing hits 3 -> self-heal */
    assert_int_equal(r->status, 0u);
    assert_int_equal(r->extended.aged_counter, 1u);
    assert_int_equal(r->extended.ageing_counter, 0u);
    assert_int_equal(r->extended.fault_occur_counter, 1u);
    assert_int_equal(r->snapshot_valid, 0u);
}

static void test_store_clear_resets_snapshot_and_extended(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 40u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);
    uds_dtc_snapshot_t env = sample_env();
    uds_dtc_store_set_environment(&s, &env);
    confirm_dtc(&s, 0x111111u);
    uds_dtc_store_clear(&s, 0xFFFFFFu);

    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_int_equal(r->extended.fault_occur_counter, 0u);
    assert_int_equal(r->extended.fault_pending_counter, 0u);
    assert_int_equal(r->extended.aged_counter, 0u);
    assert_int_equal(r->extended.ageing_counter, 0u);
    assert_int_equal(r->aging_counter, 0u);
    assert_int_equal(r->snapshot_valid, 0u);
    assert_int_equal(r->snapshot.voltage, 0u);
}

static void test_store_read_dtc_04_and_06(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 40u);
    uds_dtc_store_register(&s, 0x012345u, 0, 0, 0);
    uds_dtc_snapshot_t env = sample_env();
    uds_dtc_store_set_environment(&s, &env);
    confirm_dtc(&s, 0x012345u);

    uint8_t snap[32];
    int n = call_snapshot(&s, 0x012345u, 0x01u, snap, (uint16_t) sizeof(snap));
    assert_int_equal(n, 15);
    assert_int_equal(snap[1], 0x01u); /* record number */
    assert_int_equal(snap[2], 0x02u); /* two data identifiers */
    assert_int_equal(snap[3], 0x10u);
    assert_int_equal(snap[4], 0x01u); /* DID 0x1001 time */
    assert_int_equal(snap[5], 0x19u); /* year */
    assert_int_equal(snap[6], 0x06u);
    assert_int_equal(snap[7], 0x13u);
    assert_int_equal(snap[8], 0x0Du);
    assert_int_equal(snap[9], 0x05u);
    assert_int_equal(snap[10], 0x2Au); /* second */
    assert_int_equal(snap[11], 0x10u);
    assert_int_equal(snap[12], 0x02u); /* DID 0x1002 environment */
    assert_int_equal(snap[13], 0x8Cu);
    assert_int_equal(snap[14], 0x02u);

    memset(snap, 0, sizeof(snap));
    n = call_snapshot(&s, 0x012345u, 0xFFu, snap, (uint16_t) sizeof(snap));
    assert_int_equal(n, 15);
    assert_int_equal(snap[1], 0x01u); /* 0xFF reports the stored record number */

    uint8_t ext[8];
    n = call_extdata(&s, 0x012345u, 0x01u, ext, (uint16_t) sizeof(ext));
    assert_int_equal(n, 6);
    assert_int_equal(ext[1], 0x01u);
    assert_int_equal(ext[2], 1u); /* occurrence */
    assert_int_equal(ext[3], 1u); /* pending */
    assert_int_equal(ext[4], 0u); /* aged */
    assert_int_equal(ext[5], 0u); /* ageing */

    n = call_extdata(&s, 0x012345u, 0xFFu, ext, (uint16_t) sizeof(ext));
    assert_int_equal(n, 6);
    assert_int_equal(ext[1], 0x01u);
    assert_int_equal(ext[2], 1u);

    assert_int_equal(call_snapshot(&s, 0x999999u, 0x01u, snap, (uint16_t) sizeof(snap)),
                     -(int) UDS_NRC_REQUEST_OUT_OF_RANGE);
    assert_int_equal(call_extdata(&s, 0x999999u, 0x01u, ext, (uint16_t) sizeof(ext)),
                     -(int) UDS_NRC_REQUEST_OUT_OF_RANGE);
    assert_int_equal(call_snapshot(&s, 0x012345u, 0x02u, snap, (uint16_t) sizeof(snap)),
                     -(int) UDS_NRC_REQUEST_OUT_OF_RANGE);
    assert_int_equal(call_extdata(&s, 0x012345u, 0x02u, ext, (uint16_t) sizeof(ext)),
                     -(int) UDS_NRC_REQUEST_OUT_OF_RANGE);
}

static void test_store_snapshot_absent_is_status_only(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 40u);
    uds_dtc_store_register(&s, 0x012345u, 0, 0, 0);

    uint8_t snap[8];
    int n = call_snapshot(&s, 0x012345u, 0x01u, snap, (uint16_t) sizeof(snap));
    assert_int_equal(n, 1);
    assert_int_equal(snap[0], 0u);
}

static void test_store_persist_v1_and_snapshot(void **state)
{
    (void) state;
    uds_dtc_record_t src_mem[1];
    uds_dtc_store_t src;
    uds_dtc_store_init(&src, src_mem, 1u, 40u);
    uds_dtc_store_register(&src, 0x012345u, 0x80u, 0x10u, 0u);
    uds_dtc_snapshot_t env = sample_env();
    uds_dtc_store_set_environment(&src, &env);
    confirm_dtc(&src, 0x012345u);
    uds_dtc_store_operation_cycle(&src);
    uds_dtc_store_operation_cycle(&src); /* ageing_counter = 1 */

    uint8_t blob[40];
    int n = uds_dtc_store_serialize(&src, blob, (uint16_t) sizeof(blob));
    assert_int_equal(n, 3 + 18);
    assert_int_equal(blob[0], 0x02u);
    assert_int_equal(blob[8], 1u);  /* ageing after one clean cycle */
    assert_int_equal(blob[9], 1u);  /* occurrence */
    assert_int_equal(blob[10], 0u); /* pending cleared on the clean cycle */
    assert_int_equal(blob[12], 1u); /* snapshot_valid */

    uds_dtc_record_t dst_mem[1];
    uds_dtc_store_t dst;
    uds_dtc_store_init(&dst, dst_mem, 1u, 40u);
    uds_dtc_store_register(&dst, 0x012345u, 0x11u, 0x22u, 0u);
    assert_int_equal(uds_dtc_store_deserialize(&dst, blob, (uint16_t) n), 1);
    uds_dtc_record_t *r = uds_dtc_store_get(&dst, 0x012345u);
    assert_int_equal(r->extended.fault_occur_counter, 1u);
    assert_int_equal(r->extended.ageing_counter, r->aging_counter);
    assert_int_equal(r->aging_counter, 1u);
    assert_int_equal(r->snapshot_valid, 1u);
    assert_int_equal(r->snapshot.voltage, 0x8Cu);
    assert_int_equal(r->snapshot.time.second, 0x2Au);
    assert_int_equal(r->severity, 0x11u);

    /* Version 0x01 blob restores only the original six-byte record. */
    uint8_t v1[] = {0x01u, 0x00u, 0x01u, 0x01u, 0x23u, 0x45u, 0x08u, 0x04u, 0x02u};
    uds_dtc_record_t old_mem[1];
    uds_dtc_store_t old;
    uds_dtc_store_init(&old, old_mem, 1u, 40u);
    uds_dtc_store_register(&old, 0x012345u, 0x80u, 0x10u, 0u);
    assert_int_equal(uds_dtc_store_deserialize(&old, v1, (uint16_t) sizeof(v1)), 1);
    uds_dtc_record_t *o = uds_dtc_store_get(&old, 0x012345u);
    assert_int_equal(o->status, 0x08u);
    assert_int_equal(o->fault_detection_counter, 0x04);
    assert_int_equal(o->aging_counter, 0x02u);
    assert_int_equal(o->extended.ageing_counter, 0x02u);
    assert_int_equal(o->extended.fault_occur_counter, 0u);
    assert_int_equal(o->snapshot_valid, 0u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_store_register_and_get),
        cmocka_unit_test(test_store_register_full),
        cmocka_unit_test(test_store_fault_counter_confirms),
        cmocka_unit_test(test_store_aging_self_heal),
        cmocka_unit_test(test_store_persist_round_trip),
        cmocka_unit_test(test_store_persist_rejects_bad_input),
        cmocka_unit_test(test_store_persist_skips_unknown_and_keeps_absent),
        cmocka_unit_test(test_store_persist_empty_and_24bit_dtc),
        cmocka_unit_test(test_store_snapshot_and_extended_on_confirm),
        cmocka_unit_test(test_store_aged_counter_on_self_heal),
        cmocka_unit_test(test_store_clear_resets_snapshot_and_extended),
        cmocka_unit_test(test_store_read_dtc_04_and_06),
        cmocka_unit_test(test_store_snapshot_absent_is_status_only),
        cmocka_unit_test(test_store_persist_v1_and_snapshot),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
