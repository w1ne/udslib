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

static void test_dtc_category_examples(void **state)
{
    (void) state;
    assert_int_equal(uds_dtc_category(0x012345u), UDS_DTC_POWERTRAIN);
    assert_int_equal(uds_dtc_category(0xDCBA98u), UDS_DTC_NETWORK);
    assert_int_equal(uds_dtc_category(0xFFFFFFu), UDS_DTC_NETWORK);
}

static void test_dtc_category_boundaries(void **state)
{
    (void) state;
    assert_int_equal(uds_dtc_category(0x400000u), UDS_DTC_CHASSIS); /* 01.. */
    assert_int_equal(uds_dtc_category(0x800000u), UDS_DTC_BODY);    /* 10.. */
    assert_int_equal(uds_dtc_category(0xC00000u), UDS_DTC_NETWORK); /* 11.. */
}

static void test_dtc_status_bits(void **state)
{
    (void) state;
    assert_int_equal(UDS_DTC_STATUS_TEST_FAILED, 0x01u);
    assert_int_equal(UDS_DTC_STATUS_CONFIRMED, 0x08u);
    assert_int_equal(UDS_DTC_STATUS_WARNING_INDICATOR_REQUESTED, 0x80u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dtc_category_examples),
        cmocka_unit_test(test_dtc_category_boundaries),
        cmocka_unit_test(test_dtc_status_bits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
