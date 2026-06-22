/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_87.c
 * @brief LinkControl (0x87) — verify/transition baud-rate handshake.
 */

#include "test_helpers.h"

static uint8_t g_lc_subfn;
static uint32_t g_lc_param;
static int g_lc_calls;

static int mock_link_control(struct uds_ctx *ctx, uint8_t subfn, uint32_t param)
{
    (void) ctx;
    g_lc_subfn = subfn;
    g_lc_param = param;
    g_lc_calls++;
    return 0;
}

static void test_link_control_verify_then_transition(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = mock_link_control;
    g_lc_calls = 0;

    /* 1. verifyModeTransitionWithFixedParameter (0x01) + mode id 0x11. */
    uint8_t verify[] = {0x87, 0x01, 0x11};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* C7 01 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, verify, sizeof(verify));

    assert_int_equal(g_tx_buf[0], 0xC7);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_true(ctx.server.link_ctrl_verified);
    assert_int_equal(g_lc_subfn, 0x01);
    assert_int_equal(g_lc_param, 0x11);

    /* 2. transitionMode (0x03): applies the latched parameter. */
    uint8_t transition[] = {0x87, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* C7 03 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, transition, sizeof(transition));

    assert_int_equal(g_tx_buf[0], 0xC7);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_lc_subfn, 0x03);
    assert_int_equal(g_lc_param, 0x11); /* latched from the verify step */
    assert_false(ctx.server.link_ctrl_verified);
}

static void test_link_control_transition_without_verify_rejected(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = mock_link_control;
    g_lc_calls = 0;

    /* transitionMode with no preceding verify -> requestSequenceError (0x24). */
    uint8_t transition[] = {0x87, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 87 24 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, transition, sizeof(transition));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x87);
    assert_int_equal(g_tx_buf[2], 0x24);
    assert_int_equal(g_lc_calls, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_link_control_verify_then_transition),
        cmocka_unit_test(test_link_control_transition_without_verify_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
