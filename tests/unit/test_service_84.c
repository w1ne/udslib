/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

/* A SECURED-only inner service: reachable only when unwrapped from 0x84.
 * Echoes SID+0x40 and a marker byte. */
static void mock_secured_svc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    ctx->config->tx_buffer[1] = 0x42u;
    uds_ok(out, 2u);
}

static const uds_service_entry_t k_user_svcs[] = {
    {0xBAu, 1u, UDS_SESSION_SECURED, 0u, mock_secured_svc, NULL, 0u},
};

/* Reversible test "crypto": decode XORs with 0xFF, encode XORs with 0xAA. */
static int xor_decode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                      uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    assert_int_equal(apar, 0x1234u);
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
    assert_int_equal(apar, 0x1234u);
    assert_true(in_len <= out_max);
    for (uint16_t i = 0u; i < in_len; i++) {
        out[i] = (uint8_t) (in[i] ^ 0xAAu);
    }
    return (int) in_len;
}

static void test_secured_roundtrip(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_user_svcs;
    cfg.user_service_count = 1u;
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = xor_encode;

    /* inner = {0xBA, 0x01}; secured payload = inner ^ 0xFF = {0x45, 0xFE}. */
    uint8_t req[] = {0x84, 0x12, 0x34, 0x45, 0xFE};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 5); /* C4 APAR(2) + 2-byte secured response */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    /* inner response {0xFA, 0x42} ^ 0xAA = {0x50, 0xE8}; wrapped as C4 12 34. */
    assert_int_equal(g_tx_buf[0], 0xC4);
    assert_int_equal(g_tx_buf[1], 0x12);
    assert_int_equal(g_tx_buf[2], 0x34);
    assert_int_equal(g_tx_buf[3], 0x50);
    assert_int_equal(g_tx_buf[4], 0xE8);
}

static void test_secured_gating_direct_rejected(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_user_svcs;
    cfg.user_service_count = 1u;

    /* 0xBA sent directly (not wrapped) -> SECURED-only -> NRC 0x7F. */
    uint8_t req[] = {0xBA, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0xBA);
    assert_int_equal(g_tx_buf[2], 0x7F); /* serviceNotSupportedInActiveSession */
}

static void test_secured_no_hook(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* No fn_secure_decode configured -> cannot process -> NRC 0x22. */
    uint8_t req[] = {0x84, 0x12, 0x34, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x22); /* conditionsNotCorrect */
}

/* decode that yields an inner request which is itself 0x84 (illegal nesting). */
static int decode_to_nested_84(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                               uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) out_max;
    out[0] = 0x84u;
    out[1] = 0x00u;
    return (in_len < 2) ? 2 : (int) in_len;
}

static void test_secured_recursion_rejected(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = decode_to_nested_84;
    cfg.fn_secure_encode = xor_encode;

    uint8_t req[] = {0x84, 0x00, 0x00, 0x11, 0x22};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x84);
    assert_int_equal(g_tx_buf[2], 0x31); /* requestOutOfRange */
}

/* decode that rejects (bad MAC) by returning a negative NRC. */
static int decode_bad_mac(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                          uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) in_len;
    (void) out;
    (void) out_max;
    return -(int) 0x33; /* securityAccessDenied */
}

static void test_secured_bad_mac(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = decode_bad_mac;
    cfg.fn_secure_encode = xor_encode;

    uint8_t req[] = {0x84, 0x00, 0x00, 0xDE, 0xAD};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[2], 0x33);
}

/* Inner service that fills tx_buffer with 257 bytes — larger than UDS_SECURE_SCRATCH (256). */
static void mock_large_response_svc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                    uds_result_t *out)
{
    (void) data;
    (void) len;
    memset(ctx->config->tx_buffer, 0xABu, 257u);
    uds_ok(out, 257u);
}

static const uds_service_entry_t k_large_svc[] = {
    {0xBBu, 1u, UDS_SESSION_SECURED, 0u, mock_large_response_svc, NULL, 0u},
};

static void test_secured_inner_response_too_long(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_large_svc;
    cfg.user_service_count = 1u;
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = xor_encode;

    /* inner = {0xBB, 0x01}; secured payload = inner ^ 0xFF = {0x44, 0xFE}. */
    uint8_t req[] = {0x84, 0x12, 0x34, 0x44, 0xFE};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 84 14 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x84);
    assert_int_equal(g_tx_buf[2], 0x14); /* responseTooLong */
}

/* Regression: 0x84 wrapping an inner request with suppressPosRsp set must emit
 * NOTHING — not even a zero-length frame.  Any call to mock_tp_send will fail
 * the test because no expect_*() is registered for it. */
static void test_secured_inner_suppressed_emits_nothing(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = xor_encode;

    /* Inner = {0x3E, 0x80} (TesterPresent with suppressPosRsp bit set).
     * Secured payload = inner ^ 0xFF = {0xC1, 0x7F}. */
    uint8_t req[] = {0x84, 0x12, 0x34, 0xC1, 0x7F};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No expect_*() for mock_tp_send: any outer send is a test failure. */

    uds_input_sdu(&ctx, req, 5);
}

/* ------------------------------------------------------------------ */
/* Nested 0x84 ECU-reset ordering (regression for the Phase-1 collapse  */
/* of the captured-reset deferral). A 0x11 unwrapped from a 0x84 must    */
/* emit the secured response BEFORE fn_reset, and must NOT reset at all  */
/* when the outer secured response fails to encode.                      */
/* ------------------------------------------------------------------ */
static int g_order[4];
static int g_order_n;
static uint8_t g_secured_tx[64];

static int recording_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    if (g_order_n < 4) {
        g_order[g_order_n++] = 1; /* 1 = response emitted */
    }
    for (uint16_t i = 0u; i < len && i < (uint16_t) sizeof(g_secured_tx); i++) {
        g_secured_tx[i] = data[i];
    }
    return 0;
}

static void recording_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    if (g_order_n < 4) {
        g_order[g_order_n++] = 2; /* 2 = fn_reset called */
    }
}

static int failing_encode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                          uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) in_len;
    (void) out;
    (void) out_max;
    return -0x22; /* conditionsNotCorrect */
}

static void test_secured_inner_reset_responds_before_reset(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = xor_encode;
    cfg.fn_tp_send = recording_send;
    cfg.fn_reset = recording_reset;
    g_order_n = 0;

    /* Inner = {0x11, 0x01} (ECUReset hardReset); encoded = inner ^ 0xFF. */
    uint8_t req[] = {0x84, 0x12, 0x34, (uint8_t) (0x11u ^ 0xFFu), (uint8_t) (0x01u ^ 0xFFu)};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    uds_input_sdu(&ctx, req, sizeof(req));

    /* Secured response MUST be on the wire before fn_reset runs. */
    assert_int_equal(g_order_n, 2);
    assert_int_equal(g_order[0], 1); /* response emitted ... */
    assert_int_equal(g_order[1], 2); /* ... then reset */
    assert_int_equal(g_secured_tx[0], 0xC4u);
    /* secured body = inner response {0x51,0x01} ^ 0xAA */
    assert_int_equal(g_secured_tx[3], (uint8_t) (0x51u ^ 0xAAu));
    assert_int_equal(g_secured_tx[4], (uint8_t) (0x01u ^ 0xAAu));
}

static void test_secured_inner_reset_cancelled_on_encode_fail(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = xor_decode;
    cfg.fn_secure_encode = failing_encode; /* outer secured response cannot be built */
    cfg.fn_tp_send = recording_send;
    cfg.fn_reset = recording_reset;
    g_order_n = 0;

    uint8_t req[] = {0x84, 0x12, 0x34, (uint8_t) (0x11u ^ 0xFFu), (uint8_t) (0x01u ^ 0xFFu)};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    uds_input_sdu(&ctx, req, sizeof(req));

    /* Tester gets 7F 84 22; the ECU must NOT reboot (no reset event recorded). */
    assert_int_equal(g_order_n, 1);
    assert_int_equal(g_order[0], 1);
    assert_int_equal(g_secured_tx[0], 0x7Fu);
    assert_int_equal(g_secured_tx[1], 0x84u);
    assert_int_equal(g_secured_tx[2], 0x22u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_secured_inner_reset_responds_before_reset),
        cmocka_unit_test(test_secured_inner_reset_cancelled_on_encode_fail),
        cmocka_unit_test(test_secured_roundtrip),
        cmocka_unit_test(test_secured_gating_direct_rejected),
        cmocka_unit_test(test_secured_no_hook),
        cmocka_unit_test(test_secured_recursion_rejected),
        cmocka_unit_test(test_secured_bad_mac),
        cmocka_unit_test(test_secured_inner_suppressed_emits_nothing),
        cmocka_unit_test(test_secured_inner_response_too_long),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
