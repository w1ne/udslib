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

static int mock_dtc_clear_cb(uds_ctx_t *ctx, uint32_t group)
{
    (void) ctx;
    (void) group;
    return UDS_OK;
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

/*
 * Regression for issue #80: an ECU reset with the suppressPosRsp bit
 * (0x11 0x81) must not leak its suppress flag into the next request. The
 * handler returns without sending a response, so the suppress flag — if not
 * scoped to the request — stays set and silently swallows the response of the
 * following service. The follow-up is a non-subfunction service (0x14
 * ClearDiagnosticInformation), which never re-writes the suppress flag during
 * dispatch, so it is the case that actually breaks.
 */
static void test_ecu_reset_suppress_does_not_leak(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_reset = mock_reset_cb;
    cfg.fn_dtc_clear = mock_dtc_clear_cb;
    g_reset_called = 0;

    /* 1. Suppressed hard reset (0x11 0x81): no response is emitted. */
    uint8_t reset_req[] = {0x11, 0x81};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    uds_input_sdu(&ctx, reset_req, sizeof(reset_req));
    assert_int_equal(g_reset_called, 1);

    /* 2. The next service MUST still respond. 0x14 0xFFFFFF -> 0x54. */
    uint8_t clear_req[] = {0x14, 0xFF, 0xFF, 0xFF};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, clear_req, sizeof(clear_req));

    assert_int_equal(g_tx_buf[0], 0x54);
}

/* ISO 14229-1: the enableRapidPowerShutDown (0x11 sub 0x04) positive response
 * carries an extra powerDownTime byte -> {0x51, 0x04, powerDownTime} (3 bytes),
 * sourced from cfg.power_down_time. Other reset types stay 2 bytes. */
static void test_ecu_reset_rapid_shutdown_power_down_time(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_reset = mock_reset_cb;
    cfg.power_down_time = 0x05u;
    g_reset_called = 0;

    uint8_t request[] = {0x11, 0x04};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 51 04 <powerDownTime> */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x51);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(g_tx_buf[2], 0x05);
    assert_int_equal(g_reset_called, 1);
    assert_int_equal(g_last_reset_type, 0x04);
}

/* Reversible test "crypto" for the 0x84 secured wrapper: decode XORs 0xFF,
 * encode XORs 0xAA (mirrors tests/unit/test_service_84.c). */
static int xor_decode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                      uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    assert_true(in_len <= out_max);
    for (uint16_t i = 0u; i < in_len; i++) {
        out[i] = (uint8_t) (in[i] ^ 0xFFu);
    }
    return (int) in_len;
}

static int xor_encode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                      uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    assert_true(in_len <= out_max);
    for (uint16_t i = 0u; i < in_len; i++) {
        out[i] = (uint8_t) (in[i] ^ 0xAAu);
    }
    return (int) in_len;
}

/* A failing transport: a reset must NOT run if the response could not be sent. */
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

/* ECUReset wrapped in SecuredDataTransmission (0x84): the response the tester
 * receives is the OUTER secured frame, sent after the inner 0x51 is captured.
 * The reset must fire only after that outer frame is on the wire — proving the
 * reset is deferred, not run during inner dispatch. */
static void test_ecu_reset_secured_defers_until_outer_response(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send = order_tp_send; /* record send vs reset order */
    cfg.fn_reset = order_reset_cb;
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = xor_encode;
    g_order_seq = 0;
    g_send_order = 0;
    g_reset_order = 0;

    /* inner = {0x11, 0x01}; secured payload = inner ^ 0xFF = {0xEE, 0xFE}. */
    uint8_t req[] = {0x84, 0x12, 0x34, 0xEE, 0xFE};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */

    uds_input_sdu(&ctx, req, sizeof(req));

    /* Outer secured positive response on the wire: 0xC4 APAR(0x1234) +
     * (inner {0x51,0x01} ^ 0xAA) = {0xFB, 0xAB}. */
    assert_int_equal(g_tx_buf[0], 0xC4);
    assert_int_equal(g_tx_buf[3], 0xFB);
    assert_int_equal(g_tx_buf[4], 0xAB);
    /* Exactly one real transmit (the inner 0x51 was captured, not sent). */
    assert_int_equal(g_send_order, 1);
    /* Reset fired strictly after the outer secured response was sent. */
    assert_int_not_equal(g_reset_order, 0);
    assert_true(g_send_order < g_reset_order);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ecu_reset_response_sent_before_reset),
        cmocka_unit_test(test_ecu_reset_hard_success),
        cmocka_unit_test(test_ecu_reset_invalid_subfunction_nrc),
        cmocka_unit_test(test_ecu_reset_suppress_pos_resp),
        cmocka_unit_test(test_ecu_reset_suppress_does_not_leak),
        cmocka_unit_test(test_ecu_reset_rapid_shutdown_power_down_time),
        cmocka_unit_test(test_ecu_reset_no_reset_when_send_fails),
        cmocka_unit_test(test_ecu_reset_secured_defers_until_outer_response),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
