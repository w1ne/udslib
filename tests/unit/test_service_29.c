/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_29.c
 * @brief Unit tests for Authentication (SID 0x29) — ISO 14229-1:2020 numbering.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int mock_auth_callback(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                              uint8_t *out_buf, uint16_t max_len)
{
    (void) data;
    (void) len;
    (void) max_len;

    if (subfn == UDS_AUTH_VERIFY_CERT_UNI) {
        out_buf[0] = 0x01; /* evaluation status: valid */
        return 1;
    }

    if (subfn == UDS_AUTH_PROOF_OF_OWNERSHIP) {
        ctx->authenticated = true; /* app's crypto verified ownership */
        out_buf[0] = 0x02;         /* ARP: ownershipVerified */
        return 1;
    }

    return -0x22; /* conditionsNotCorrect for other sub-functions */
}

/* deAuthenticate (0x00) is native: clears state, returns 69 00 10. */
static void test_auth_deauthenticate_native(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.authenticated = true;

    uint8_t req[] = {0x29, UDS_AUTH_DEAUTHENTICATE};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 69 00 10 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[0], 0x69);
    assert_int_equal(g_tx_buf[1], 0x00);
    assert_int_equal(g_tx_buf[2], 0x10);
    assert_false(ctx.authenticated);
}

/* authenticationConfiguration (0x08) reports the configured byte natively. */
static void test_auth_configuration_native(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.auth_configuration = 0x02u;

    uint8_t req[] = {0x29, UDS_AUTH_CONFIGURATION};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 69 08 02 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[1], 0x08);
    assert_int_equal(g_tx_buf[2], 0x02);
}

/* verifyCertificateUnidirectional (0x01) is delegated to fn_auth. */
static void test_auth_verify_cert_uni_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = mock_auth_callback;

    uint8_t req[] = {0x29, UDS_AUTH_VERIFY_CERT_UNI, 0xAA, 0xBB};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 69 01 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[0], 0x69);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_tx_buf[2], 0x01);
}

/* proofOfOwnership (0x03): the app verifies and sets ctx.authenticated. */
static void test_auth_proof_sets_state(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = mock_auth_callback;

    uint8_t req[] = {0x29, UDS_AUTH_PROOF_OF_OWNERSHIP, 0xCC};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 69 03 02 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[2], 0x02);
    assert_true(ctx.authenticated);
}

/* A delegated sub-function with no fn_auth -> NRC 0x22. */
static void test_auth_no_callback_nrc(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t req[] = {0x29, UDS_AUTH_VERIFY_CERT_UNI};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 29 22 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x22);
}

/* Unsupported sub-function 0x09 -> NRC 0x12. */
static void test_auth_invalid_subfn(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = mock_auth_callback;

    uint8_t req[] = {0x29, 0x09};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 29 12 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x12);
}

/* Authentication state clears on a session change. */
static void test_auth_cleared_on_session_change(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.authenticated = true;

    uint8_t req[] = {0x10, 0x03}; /* -> Extended session */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x50 03 P2 P2 P2* P2* */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_false(ctx.authenticated);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_auth_deauthenticate_native),
        cmocka_unit_test(test_auth_configuration_native),
        cmocka_unit_test(test_auth_verify_cert_uni_success),
        cmocka_unit_test(test_auth_proof_sets_state),
        cmocka_unit_test(test_auth_no_callback_nrc),
        cmocka_unit_test(test_auth_invalid_subfn),
        cmocka_unit_test(test_auth_cleared_on_session_change),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
