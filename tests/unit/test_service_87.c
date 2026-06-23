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

static bool lc_tx_complete(struct uds_ctx *ctx)
{
    (void) ctx;
    return true;
}

static void test_link_control_verify_then_transition(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = mock_link_control;
    cfg.fn_tx_complete = lc_tx_complete;
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

    /* 2. transitionMode (0x03): response goes out, switch deferred to uds_process. */
    uint8_t transition[] = {0x87, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* C7 03 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, transition, sizeof(transition));

    assert_int_equal(g_tx_buf[0], 0xC7);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_false(ctx.server.link_ctrl_verified);
    assert_int_equal(g_lc_subfn, 0x01); /* transition not yet applied */

    will_return(mock_get_time, 1000); /* uds_process drains the transition */
    uds_process(&ctx);
    assert_int_equal(g_lc_subfn, 0x03);
    assert_int_equal(g_lc_param, 0x11); /* latched from the verify step */
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

/* #98: the baud-rate transition (0x03) reconfigures the diagnostic link itself,
 * so the positive response must be on the wire BEFORE the switch is applied.
 * The library sends the response, then defers fn_link_control(0x03) until
 * uds_process observes transmit-complete. */
static void test_link_control_transition_deferred_until_tx_complete(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = mock_link_control;
    cfg.fn_tx_complete = lc_tx_complete;

    /* verifyModeTransition latches the parameter (runs synchronously). */
    uint8_t verify[] = {0x87, 0x01, 0x11};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, verify, sizeof(verify));
    assert_int_equal(g_lc_subfn, 0x01);

    /* transitionMode: response goes out, but the switch is NOT applied yet. */
    g_lc_calls = 0;
    uint8_t transition[] = {0x87, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, transition, sizeof(transition));

    assert_int_equal(g_tx_buf[0], 0xC7);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_lc_calls, 0); /* transition deferred, not applied in RX path */

    /* uds_process drains it once TX is complete. */
    will_return(mock_get_time, 1000);
    uds_process(&ctx);
    assert_int_equal(g_lc_calls, 1);
    assert_int_equal(g_lc_subfn, 0x03);
    assert_int_equal(g_lc_param, 0x11); /* latched from the verify step */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_link_control_verify_then_transition),
        cmocka_unit_test(test_link_control_transition_without_verify_rejected),
        cmocka_unit_test(test_link_control_transition_deferred_until_tx_complete),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
