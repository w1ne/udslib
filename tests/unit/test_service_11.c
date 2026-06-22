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

/* --- Ordering harness: prove the response is transmitted before the reset --- */

static int g_seq = 0;        /* monotonic event counter */
static int g_tx_seq = -1;    /* sequence number at which the response was sent */
static int g_reset_seq = -1; /* sequence number at which fn_reset fired */
static uint8_t g_tx_first_byte = 0;

static int ordering_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_tx_seq = ++g_seq;
    if (len > 0u) {
        g_tx_first_byte = data[0];
    }
    memcpy(g_tx_buf, data, len);
    return 0;
}

static void ordering_reset_cb(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    g_reset_seq = ++g_seq;
    g_reset_called++;
    g_last_reset_type = type;
}

/* ISO 14229-1:2013 9.3.2.2: the ECUReset positive response shall be sent
 * before the reset is executed. Regression guard for issue #76. */
static void test_ecu_reset_response_precedes_reset(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send = ordering_tp_send; /* override helper mock to record order */
    cfg.fn_reset = ordering_reset_cb;
    g_seq = 0;
    g_tx_seq = -1;
    g_reset_seq = -1;
    g_reset_called = 0;
    g_tx_first_byte = 0;

    uint8_t request[] = {0x11, 0x01};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */

    uds_input_sdu(&ctx, request, sizeof(request));

    /* Positive response was transmitted... */
    assert_int_equal(g_tx_first_byte, 0x51);
    /* ...and the reset hook fired... */
    assert_int_equal(g_reset_called, 1);
    assert_int_equal(g_last_reset_type, 0x01);
    /* ...with the response strictly before the reset. */
    assert_true(g_tx_seq > 0);
    assert_true(g_reset_seq > 0);
    assert_true(g_tx_seq < g_reset_seq);
}

/* A reset hook must NOT run if the response could not be handed to the
 * transport, so the tester is never left without a confirmation. */
static int failing_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return -1;
}

static void test_ecu_reset_no_reset_when_send_fails(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send = failing_tp_send;
    cfg.fn_reset = mock_reset_cb;
    g_reset_called = 0;

    uint8_t request[] = {0x11, 0x01};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_reset_called, 0);
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
        cmocka_unit_test(test_ecu_reset_hard_success),
        cmocka_unit_test(test_ecu_reset_invalid_subfunction_nrc),
        cmocka_unit_test(test_ecu_reset_suppress_pos_resp),
        cmocka_unit_test(test_ecu_reset_response_precedes_reset),
        cmocka_unit_test(test_ecu_reset_no_reset_when_send_fails),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
