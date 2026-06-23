/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_p2_single_source.c
 * @brief Regression tests for §5a: P2/P2* single source of truth.
 *
 * Invariant: uds_init() resolves P2/P2* exactly once into ctx->session.p2_ms /
 * ctx->session.p2_star_ms.  The 0x10 positive response MUST derive its
 * advertised timings from those resolved ctx values, not by re-reading config
 * fields.
 *
 * Precedence (enforced in uds_init):
 *   1. p2_ms / p2_star_ms non-zero  →  authoritative; used as-is.
 *   2. p2_ms zero but p2_server_max non-zero  →  fold p2_server_max in
 *      (legacy alias).
 *   3. Both zero  →  50 ms / 5000 ms defaults.
 */

#include "test_helpers.h"

/* Helper: build a custom context without the BEGIN_UDS_TEST / setup_ctx
 * defaults so each test can set p2 fields from scratch. */
static void init_ctx_custom(uds_ctx_t *ctx, uds_config_t *cfg, uint16_t p2_ms,
                             uint32_t p2_star_ms, uint16_t p2_server_max,
                             uint16_t p2_star_server_max)
{
    memset(cfg, 0, sizeof(uds_config_t));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_rx_buf;
    cfg->rx_buffer_size = sizeof(g_rx_buf);
    cfg->tx_buffer = g_tx_buf;
    cfg->tx_buffer_size = sizeof(g_tx_buf);
    cfg->p2_ms = p2_ms;
    cfg->p2_star_ms = p2_star_ms;
    cfg->p2_server_max = p2_server_max;
    cfg->p2_star_server_max = p2_star_server_max;
    uds_init(ctx, cfg);
}

/* Helper: send 0x10 0x03 and capture response bytes 2..5 (P2 hi, P2 lo,
 * P2* hi, P2* lo). */
static void send_session_request(uds_ctx_t *ctx)
{
    uint8_t req[] = {0x10, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, req, sizeof(req));
}

/*
 * Case A: only p2_ms / p2_star_ms set (authoritative path).
 * Configured: p2_ms=100, p2_star_ms=2000.
 * Expected in 0x10 response: P2 bytes = 0x00 0x64 (100),
 *                             P2* bytes = 0x00 0xC8 (2000/10 = 200 = 0x00C8).
 */
static void test_p2_ms_authoritative(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_custom(&ctx, &cfg, 100u, 2000u, 0u, 0u);

    /* Verify ctx fields are resolved correctly at init */
    assert_int_equal(ctx.session.p2_ms, 100u);
    assert_int_equal(ctx.session.p2_star_ms, 2000u);

    send_session_request(&ctx);

    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[2], 0x00);
    assert_int_equal(g_tx_buf[3], 0x64); /* P2 = 100 ms */
    assert_int_equal(g_tx_buf[4], 0x00);
    assert_int_equal(g_tx_buf[5], 0xC8); /* P2* = 200 units of 10ms (2000ms) */
}

/*
 * Case B: neither p2_ms nor p2_server_max set — defaults kick in.
 * Expected: P2 = 50 ms (0x00 0x32), P2* = 500 units (5000/10 = 0x01F4).
 */
static void test_p2_default_values(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_custom(&ctx, &cfg, 0u, 0u, 0u, 0u);

    assert_int_equal(ctx.session.p2_ms, 50u);
    assert_int_equal(ctx.session.p2_star_ms, 5000u);

    send_session_request(&ctx);

    assert_int_equal(g_tx_buf[2], 0x00);
    assert_int_equal(g_tx_buf[3], 0x32); /* P2 = 50 ms */
    assert_int_equal(g_tx_buf[4], 0x01);
    assert_int_equal(g_tx_buf[5], 0xF4); /* P2* = 500 units (5000ms) */
}

/*
 * Case C: legacy-only path — only p2_server_max / p2_star_server_max set,
 * p2_ms / p2_star_ms both zero.  uds_init must fold them in.
 * Configured: p2_server_max=200, p2_star_server_max=10000.
 * Expected: ctx.session.p2_ms = 200, ctx.session.p2_star_ms = 10000,
 *           and 0x10 response echoes exactly those.
 */
static void test_p2_legacy_fold_in(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_custom(&ctx, &cfg, 0u, 0u, 200u, 10000u);

    assert_int_equal(ctx.session.p2_ms, 200u);
    assert_int_equal(ctx.session.p2_star_ms, 10000u);

    send_session_request(&ctx);

    assert_int_equal(g_tx_buf[2], 0x00);
    assert_int_equal(g_tx_buf[3], 0xC8); /* P2 = 200 ms */
    assert_int_equal(g_tx_buf[4], 0x03);
    assert_int_equal(g_tx_buf[5], 0xE8); /* P2* = 1000 units (10000ms) */
}

/*
 * Case D: p2_ms takes precedence over p2_server_max when both are set.
 * p2_ms=75, p2_server_max=200 → resolved P2 must be 75, not 200.
 */
static void test_p2_ms_wins_over_server_max(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_custom(&ctx, &cfg, 75u, 3000u, 200u, 10000u);

    assert_int_equal(ctx.session.p2_ms, 75u);
    assert_int_equal(ctx.session.p2_star_ms, 3000u);

    send_session_request(&ctx);

    assert_int_equal(g_tx_buf[2], 0x00);
    assert_int_equal(g_tx_buf[3], 0x4B); /* P2 = 75 ms */
    assert_int_equal(g_tx_buf[4], 0x01);
    assert_int_equal(g_tx_buf[5], 0x2C); /* P2* = 300 units (3000ms) */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_p2_ms_authoritative),
        cmocka_unit_test(test_p2_default_values),
        cmocka_unit_test(test_p2_legacy_fold_in),
        cmocka_unit_test(test_p2_ms_wins_over_server_max),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
