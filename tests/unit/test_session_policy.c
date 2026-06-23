/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_session_policy.c
 * @brief Opt-in built-in session policy (config.restrict_sessions).
 */

#include "test_helpers.h"

#define NRC_SERVICE_NOT_SUPP_IN_SESSION 0x7Fu
#define NRC_CONDITIONS_NOT_CORRECT 0x22u

/* SecurityAccess (0x27) is privileged: with the policy on it must be rejected
   in the default session and reach the handler in the extended session. With a
   handler but no seed callback it returns 0x22 -- proof it passed the session
   gate. */
static void test_security_access_blocked_in_default_when_restricted(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.restrict_sessions = true;
    /* active_session defaults to 0x01 (default) after init. */

    uint8_t req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 7F */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x27);
    assert_int_equal(g_tx_buf[2], NRC_SERVICE_NOT_SUPP_IN_SESSION);
}

static void test_security_access_allowed_in_extended_when_restricted(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.restrict_sessions = true;
    ctx.session.active = 0x03; /* extended */

    uint8_t req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000); /* security handler reads time */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 22 (no seed cb) -> passed session gate */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[2], NRC_CONDITIONS_NOT_CORRECT);
}

/* Default (flag off) keeps the permissive behavior: SecurityAccess reaches the
   handler even in the default session. */
static void test_security_access_reachable_in_default_when_unrestricted(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* cfg.restrict_sessions left false */

    uint8_t req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 22 (reached handler) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[2], NRC_CONDITIONS_NOT_CORRECT);
}

/* RequestDownload (0x34) is programming-only under the policy. */
static void test_download_blocked_in_extended_when_restricted(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.restrict_sessions = true;
    ctx.session.active = 0x03; /* extended -> still not allowed for download */

    uint8_t req[] = {0x34, 0x00, 0x11, 0x22};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 34 7F */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x34);
    assert_int_equal(g_tx_buf[2], NRC_SERVICE_NOT_SUPP_IN_SESSION);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_security_access_blocked_in_default_when_restricted),
        cmocka_unit_test(test_security_access_allowed_in_extended_when_restricted),
        cmocka_unit_test(test_security_access_reachable_in_default_when_unrestricted),
        cmocka_unit_test(test_download_blocked_in_extended_when_restricted),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
