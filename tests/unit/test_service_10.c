/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_10.c
 * @brief Unit tests for SID 0x10 (Diagnostic Session Control)
 */

#include "test_helpers.h"

static void test_extended_session_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t request[] = {0x10, 0x03};

    will_return(mock_get_time, 1000); /* Input handler */
    will_return(mock_get_time, 1000); /* Dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x50 03 P2 P2 P2* P2* */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x03);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x03);
}

static void test_default_session_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.active_session = 0x03;

    uint8_t request[] = {0x10, 0x01};

    will_return(mock_get_time, 2000); /* Input */
    will_return(mock_get_time, 2000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x01);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x01);
}

/* $04 safetySystemDiagnosticSession is a standard ISO 14229-1 session type. */
static void test_safety_session_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t request[] = {0x10, 0x04};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x50 04 P2 P2 P2* P2* */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x04);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x04);
}

/* --- Opt-in session-transition policy hook --- */

static int g_transition_from;
static int g_transition_to;

static bool transition_deny_default_to_programming(uds_ctx_t *ctx, uint8_t from, uint8_t to)
{
    (void) ctx;
    g_transition_from = from;
    g_transition_to = to;
    /* OEM-style graph: programming may only be entered from extended. */
    if (to == 0x02 && from != 0x03) {
        return false;
    }
    return true;
}

/* With no hook configured (default), ISO behavior: any session may be entered
   from any session -- including default -> programming directly. */
static void test_no_hook_allows_default_to_programming(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* active_session defaults to 0x01 (default). */

    uint8_t request[] = {0x10, 0x02};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x02);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x02);
}

/* Hook rejects default -> programming with NRC 0x22; session is unchanged. */
static void test_hook_rejects_disallowed_transition(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_session_transition_allowed = transition_deny_default_to_programming;
    g_transition_from = -1;
    g_transition_to = -1;
    /* active_session defaults to 0x01 (default). */

    uint8_t request[] = {0x10, 0x02};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 10 22 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x10);
    assert_int_equal(g_tx_buf[2], 0x22);        /* conditionsNotCorrect */
    assert_int_equal(ctx.active_session, 0x01); /* unchanged */
    /* Hook saw the correct from/to pair. */
    assert_int_equal(g_transition_from, 0x01);
    assert_int_equal(g_transition_to, 0x02);
}

/* Hook allows extended -> programming; session transitions. */
static void test_hook_allows_permitted_transition(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_session_transition_allowed = transition_deny_default_to_programming;
    ctx.active_session = 0x03; /* extended */

    uint8_t request[] = {0x10, 0x02};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x02);
    assert_int_equal(g_tx_buf[1], 0x02);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_extended_session_success),
        cmocka_unit_test(test_default_session_success),
        cmocka_unit_test(test_safety_session_success),
        cmocka_unit_test(test_no_hook_allows_default_to_programming),
        cmocka_unit_test(test_hook_rejects_disallowed_transition),
        cmocka_unit_test(test_hook_allows_permitted_transition),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
