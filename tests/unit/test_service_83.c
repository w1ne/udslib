/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_83.c
 * @brief AccessTimingParameter (0x83) — read/set the live P2/P2* timing.
 */

#include "test_helpers.h"

/* setup_ctx() configures p2_ms = 50, p2_star_ms = 5000. */

static void test_access_timing_read_current(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* readCurrentlyActiveTimingParameters (0x03). P2* reported in 10 ms units. */
    uint8_t req[] = {0x83, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* C3 03 00 32 01 F4 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC3);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[2], 0x00); /* P2 = 50 = 0x0032 */
    assert_int_equal(g_tx_buf[3], 0x32);
    assert_int_equal(g_tx_buf[4], 0x01); /* P2* = 5000ms / 10 = 500 = 0x01F4 */
    assert_int_equal(g_tx_buf[5], 0xF4);
}

static void test_access_timing_set_given(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* setTimingParametersToGivenValues (0x04): P2 = 0x0064 (100ms),
       P2* record = 0x00C8 (200 * 10ms = 2000ms). */
    uint8_t req[] = {0x83, 0x04, 0x00, 0x64, 0x00, 0xC8};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* C3 04 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC3);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(ctx.session.p2_ms, 100);
    assert_int_equal(ctx.session.p2_star_ms, 2000);
}

static void test_access_timing_set_default(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* Change the timing, then reset it. */
    ctx.session.p2_ms = 100;
    ctx.session.p2_star_ms = 2000;

    /* setTimingParametersToDefaultValues (0x02) -> back to configured defaults. */
    uint8_t req[] = {0x83, 0x02};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* C3 02 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC3);
    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(ctx.session.p2_ms, 50);
    assert_int_equal(ctx.session.p2_star_ms, 5000);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_access_timing_read_current),
        cmocka_unit_test(test_access_timing_set_given),
        cmocka_unit_test(test_access_timing_set_default),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
