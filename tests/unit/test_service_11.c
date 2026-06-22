/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_11.c
 * @brief Unit tests for SID 0x11 (ECU Reset)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static int g_reset_called = 0;
static uint8_t g_last_reset_type = 0;

static void mock_reset_cb(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    g_reset_called++;
    g_last_reset_type = type;
}

/* --- Ordering probes for issue #76 ---
 * ISO 14229-1: the ECUReset positive response SHALL be sent before the reset
 * is performed (after a real reset the server cannot answer). These local
 * callbacks stamp a monotonic counter so the test can assert send-before-reset.
 */
static int g_order_seq;
static int g_send_order;
static int g_reset_order;

static int order_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_send_order = ++g_order_seq;
    if (len > sizeof(g_tx_buf)) {
        len = (uint16_t) sizeof(g_tx_buf);
    }
    memcpy(g_tx_buf, data, len);
    return 0;
}

static void order_reset_cb(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    g_reset_order = ++g_order_seq;
}

static void test_ecu_reset_response_sent_before_reset(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send = order_tp_send; /* override the cmocka mock to record order */
    cfg.fn_reset = order_reset_cb;
    g_order_seq = 0;
    g_send_order = 0;
    g_reset_order = 0;

    uint8_t request[] = {0x11, 0x01};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */

    uds_input_sdu(&ctx, request, sizeof(request));

    /* Both happened... */
    assert_int_not_equal(g_send_order, 0);
    assert_int_not_equal(g_reset_order, 0);
    /* ...and the positive response went out BEFORE the reset was performed. */
    assert_true(g_send_order < g_reset_order);
    assert_int_equal(g_tx_buf[0], 0x51);
    assert_int_equal(g_tx_buf[1], 0x01);
}

static void test_ecu_reset_hard_success(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_reset = mock_reset_cb;
    g_reset_called = 0;

    uint8_t request[] = {0x11, 0x01};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x51);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_reset_called, 1);
    assert_int_equal(g_last_reset_type, 0x01);
}

static void test_ecu_reset_invalid_subfunction_nrc(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    g_reset_called = 0;

    uint8_t request[] = {0x11, 0x99};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x11);
    assert_int_equal(g_tx_buf[2], 0x12); /* Subfunction Not Supported */
    assert_int_equal(g_reset_called, 0);
}

static void test_ecu_reset_suppress_pos_resp(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_reset = mock_reset_cb;
    g_reset_called = 0;

    /* 0x11 0x81 -> Hard Reset (0x01) + SuppressPosResp (0x80) */
    uint8_t request[] = {0x11, 0x81};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_any(mock_tp_send, data) here because it must be suppressed */

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_reset_called, 1);
    assert_int_equal(g_last_reset_type, 0x01);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ecu_reset_response_sent_before_reset),
        cmocka_unit_test(test_ecu_reset_hard_success),
        cmocka_unit_test(test_ecu_reset_invalid_subfunction_nrc),
        cmocka_unit_test(test_ecu_reset_suppress_pos_resp),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
