/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"
#include "uds_internal.h" /* UDS_NRC_RESPONSE_TOO_LONG */

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

/* Re-registering an existing DTC updates its fields in place and returns the
 * existing index without growing the store (uds_dtc_store.c:22-27). */
static void test_store_register_updates_existing(void **state)
{
    (void) state;
    uds_dtc_record_t backing[4];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 4u, 40u);

    assert_int_equal(uds_dtc_store_register(&s, 0x012345u, 0x10u, 0x01u, UDS_DTC_FGID_EMISSIONS),
                     0);
    assert_int_equal(s.count, 1u);

    /* Same DTC again with new attributes: in-place update, index 0, count stays 1. */
    assert_int_equal(uds_dtc_store_register(&s, 0x012345u, 0x80u, 0x02u, UDS_DTC_FGID_SAFETY), 0);
    assert_int_equal(s.count, 1u);

    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x012345u);
    assert_int_equal(r->severity, 0x80u);
    assert_int_equal(r->functional_unit, 0x02u);
    assert_int_equal(r->functional_group, UDS_DTC_FGID_SAFETY);
}

/* report_test(failed) does not push the fault-detection counter past 0x7F
 * (uds_dtc_store.c:62 false branch / :59 guard). */
static void test_store_counter_saturates_high(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    for (int i = 0; i < 300; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, true);
    }
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_int_equal(r->fault_detection_counter, 0x7F); /* clamped, never wraps */
}

/* report_test(pass) decrements the counter and clears TEST_FAILED, stopping at
 * the -128 floor (uds_dtc_store.c:77-80). */
static void test_store_pass_decrements_and_floors(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    uds_dtc_store_report_test(&s, 0x111111u, true);
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_int_equal(r->fault_detection_counter, 1);
    assert_true((r->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);

    /* One pass: counter back to 0 and TEST_FAILED cleared. */
    uds_dtc_store_report_test(&s, 0x111111u, false);
    assert_int_equal(r->fault_detection_counter, 0);
    assert_false((r->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);

    /* Keep passing: counter floors at -128, never underflows. */
    for (int i = 0; i < 300; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, false);
    }
    assert_int_equal(r->fault_detection_counter, -128);
}

/* report_test / get on an unknown DTC are no-ops (uds_dtc_store.c:58 NULL path). */
static void test_store_report_unknown_is_noop(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 3u);
    /* No register: store empty. */
    uds_dtc_store_report_test(&s, 0xABCDEFu, true); /* must not crash or write */
    assert_null(uds_dtc_store_get(&s, 0xABCDEFu));
    assert_int_equal(s.count, 0u);
}

/* The reference extended-data callback: unknown DTC returns 0, a buffer too
 * small to hold the 4-byte record returns -RESPONSE_TOO_LONG, and a good call
 * writes status/record/aging/fdc (uds_dtc_store.c:139-149). */
static void test_extdata_cb_paths(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 40u);
    uds_dtc_store_register(&s, 0x012345u, 0, 0, 0);
    uds_dtc_store_report_test(&s, 0x012345u, true);

    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.app_data = &s;
    ctx.config = &cfg;

    uint8_t out[8];

    /* Unknown DTC -> 0 bytes. */
    assert_int_equal(uds_dtc_store_extdata_cb(&ctx, 0x999999u, 0x01u, out, sizeof(out)), 0);

    /* Buffer too small (< 4) -> negative responseTooLong. */
    assert_int_equal(uds_dtc_store_extdata_cb(&ctx, 0x012345u, 0x01u, out, 3u),
                     -(int) UDS_NRC_RESPONSE_TOO_LONG);

    /* Good call -> 4 bytes: status, record_num, aging, fdc. */
    assert_int_equal(uds_dtc_store_extdata_cb(&ctx, 0x012345u, 0x07u, out, sizeof(out)), 4);
    assert_int_equal(out[1], 0x07u);                          /* record number echoed */
    assert_true((out[0] & UDS_DTC_STATUS_TEST_FAILED) != 0u); /* status */
    assert_int_equal(out[3], 1);                              /* fault detection counter */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_store_register_and_get),
        cmocka_unit_test(test_store_register_full),
        cmocka_unit_test(test_store_fault_counter_confirms),
        cmocka_unit_test(test_store_aging_self_heal),
        cmocka_unit_test(test_store_register_updates_existing),
        cmocka_unit_test(test_store_counter_saturates_high),
        cmocka_unit_test(test_store_pass_decrements_and_floors),
        cmocka_unit_test(test_store_report_unknown_is_noop),
        cmocka_unit_test(test_extdata_cb_paths),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
