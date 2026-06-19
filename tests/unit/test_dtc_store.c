/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

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

    /* 3 clean operation cycles -> aging threshold -> self-heal. */
    uds_dtc_store_operation_cycle(&s);
    uds_dtc_store_operation_cycle(&s);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
    uds_dtc_store_operation_cycle(&s);
    assert_int_equal(r->status, 0u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_store_register_and_get),
        cmocka_unit_test(test_store_register_full),
        cmocka_unit_test(test_store_fault_counter_confirms),
        cmocka_unit_test(test_store_aging_self_heal),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
