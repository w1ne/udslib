/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_doc_behaviours.c
 * @brief Document-intended behaviour tests.
 *
 * These tests lock the current observed behaviour of the stack for cases
 * that might otherwise be under-specified. They MUST pass as-is: if one
 * fails, the production behaviour differs from the documented expectation
 * and must be investigated rather than having the assertion changed.
 */

#include "test_helpers.h"

/* ------------------------------------------------------------------ */
/* Test 10: comm_state persists across S3 timeout                      */
/*                                                                     */
/* S3 resets the session back to default but must NOT clear comm_state */
/* (Communication Control state is owned by the application and stays  */
/* until explicitly reset by a new 0x28 request).                      */
/* ------------------------------------------------------------------ */

static int mock_comm_control_doc(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type,
                                 uint16_t node_id)
{
    (void) ctx;
    (void) ctrl_type;
    (void) comm_type;
    (void) node_id;
    return 0;
}

static void test_doc_comm_state_persists_across_s3(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_comm_control = mock_comm_control_doc;

    /* Enter extended session at T=0 so S3 has something to reset */
    uint8_t ext_req[] = {0x10, 0x03};
    will_return(mock_get_time, 0);
    will_return(mock_get_time, 0);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, ext_req, sizeof(ext_req));

    /* Disable Rx+Tx (0x28 0x03 0x01) */
    uint8_t comm_req[] = {0x28, 0x03, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 68 03 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, comm_req, sizeof(comm_req));
    assert_int_equal(ctx.session.comm_state, 0x03);

    /* Advance past S3 (5000 ms) and fire uds_process */
    will_return(mock_get_time, 6001u);
    uds_process(&ctx);

    /* comm_state must still be 0x03: S3 does NOT reset it */
    assert_int_equal(ctx.session.comm_state, 0x03);
}

/* ------------------------------------------------------------------ */
/* Test 11: security lockout persists across session change             */
/*                                                                     */
/* Brute-force protection: once security_delay_end is set by a failed  */
/* key attempt, a session change must NOT clear it.  Any requestSeed   */
/* before the delay expires must still return NRC 0x37.                */
/* ------------------------------------------------------------------ */

static int mock_security_seed_doc(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf,
                                  uint16_t max_len)
{
    (void) ctx;
    (void) level;
    (void) max_len;
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

static int mock_security_key_doc(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                                 const uint8_t *key, uint16_t key_len)
{
    (void) ctx;
    (void) level;
    (void) seed;
    (void) key_len;
    if (key[0] == 0xDF && key[1] == 0xAE && key[2] == 0xBF && key[3] == 0xF0) return 0;
    return -0x35;
}

static void test_doc_lockout_persists_across_session_change(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed_doc;
    cfg.fn_security_key = mock_security_key_doc;
    cfg.security_max_attempts = 1u;
    cfg.security_delay_ms = 5000u;

    /* Get a seed to arm the sendKey sequence */
    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));

    /* Send wrong key -> NRC 0x36 (exceededNumberOfAttempts) -> lockout */
    uint8_t bad_key[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 36 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, bad_key, sizeof(bad_key));
    assert_int_equal(g_tx_buf[2], 0x36);
    assert_true(ctx.security.delay_end > 0u);

    /* Session change (at T=1500, still within the 5-second lockout window) */
    uint8_t sess_req[] = {0x10, 0x01};
    will_return(mock_get_time, 1500);
    will_return(mock_get_time, 1500);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, sess_req, sizeof(sess_req));

    /* RequestSeed before delay expires (T=2000 < lockout end ~6000) */
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 37 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x27);
    assert_int_equal(g_tx_buf[2], 0x37); /* requiredTimeDelayNotExpired */
}

/* ------------------------------------------------------------------ */
/* Test 12: periodic subscription survives S3 timeout                  */
/*                                                                     */
/* S3 resets the session but must NOT cancel active periodic reads.    */
/* A uds_process call after S3 + past a periodic deadline must still   */
/* fire the periodic read.                                             */
/* ------------------------------------------------------------------ */

static int mock_periodic_read_doc(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                                  uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    if (periodic_id == 0xE1u) {
        out_buf[0] = 0x11u;
        out_buf[1] = 0x22u;
        return 2;
    }
    return -1;
}

static void test_doc_periodic_survives_s3(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read_doc;

    /* Enter extended session at T=0 */
    uint8_t ext_req[] = {0x10, 0x03};
    will_return(mock_get_time, 0);
    will_return(mock_get_time, 0);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, ext_req, sizeof(ext_req));

    /* Subscribe periodic ID 0xE1 at fast rate (100 ms interval) at T=1000 */
    uint8_t per_req[] = {0x2A, 0x01, 0xE1};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 6A */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, per_req, sizeof(per_req));
    assert_int_equal(ctx.server.periodic_count, 1u);

    /* Advance past S3 at T=6001: session resets to default.
     * The periodic deadline (1000+100=1100) is already past at T=6001, so
     * uds_process also fires the periodic read in the same call. */
    will_return(mock_get_time, 6001u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* E1 11 22 */
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    /* periodic subscription must NOT have been cancelled by S3 */
    assert_int_equal(ctx.server.periodic_count, 1u);

    /* Advance to T=7200 (well past the renewed 100 ms deadline at 6001+100=6101).
     * uds_process must fire the periodic read again. */
    will_return(mock_get_time, 7200u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* E1 11 22 */
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_doc_comm_state_persists_across_s3),
        cmocka_unit_test(test_doc_lockout_persists_across_session_change),
        cmocka_unit_test(test_doc_periodic_survives_s3),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
