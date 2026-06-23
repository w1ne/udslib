/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_s3_configurable.c
 * @brief Regression tests for §5b: configurable S3 session timeout.
 *
 * Invariant: uds_init() resolves the S3 timeout once into ctx->session.s3_ms.
 * When config->s3_ms is zero the stack default (UDS_S3_TIMEOUT_MS = 5000 ms)
 * applies; otherwise the supplied value is used directly.  The session-revert
 * check in uds_process() uses the resolved ctx value exclusively.
 */

#include "test_helpers.h"

/* Helper: initialise a context with an explicit s3_ms override (0 = default). */
static void init_ctx_s3(uds_ctx_t *ctx, uds_config_t *cfg, uint32_t s3_ms)
{
    memset(cfg, 0, sizeof(uds_config_t));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_rx_buf;
    cfg->rx_buffer_size = sizeof(g_rx_buf);
    cfg->tx_buffer = g_tx_buf;
    cfg->tx_buffer_size = sizeof(g_tx_buf);
    cfg->p2_ms = 50u;
    cfg->p2_star_ms = 5000u;
    cfg->s3_ms = s3_ms;
    uds_init(ctx, cfg);
}

/*
 * Case A (custom S3): configure s3_ms = 200; enter extended session (DiagnosticSessionControl
 * sub-function 0x03); advance mock time by 201 ms; call uds_process;
 * assert session reverted to default and security relocked.
 */
static void test_s3_custom_timeout_reverts_session(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_s3(&ctx, &cfg, 200u);

    /* Verify s3 was resolved correctly */
    assert_int_equal(ctx.session.s3_ms, 200u);

    /* Drive into extended session via DiagnosticSessionControl 0x03 */
    uint8_t req[] = {0x10, 0x03};
    will_return(mock_get_time, 1000u); /* input_sdu timestamp */
    will_return(mock_get_time, 1000u); /* dispatcher timestamp */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6u);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(ctx.session.active, 0x03u); /* extended session */

    /* Fake security unlock so we can assert relock */
    ctx.security.level = 0x01u;
    ctx.security.authenticated = true;
    ctx.session.last_msg_time = 1000u;

    /* Advance time by 201 ms — past the 200 ms custom S3 */
    will_return(mock_get_time, 1201u);

    uds_process(&ctx);

    assert_int_equal(ctx.session.active, 0x01u);  /* default session */
    assert_int_equal(ctx.security.level, 0x00u);  /* security relocked */
    assert_false(ctx.security.authenticated);      /* auth cleared */
}

/*
 * Case B (custom S3, not yet expired): same 200 ms S3 but only 100 ms elapsed;
 * session must NOT revert.
 */
static void test_s3_custom_timeout_not_yet_expired(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_s3(&ctx, &cfg, 200u);

    ctx.session.active = 0x03u;
    ctx.security.level = 0x01u;
    ctx.session.last_msg_time = 1000u;

    /* Only 100 ms elapsed — S3 not expired */
    will_return(mock_get_time, 1100u);
    uds_process(&ctx);

    assert_int_equal(ctx.session.active, 0x03u); /* still extended */
    assert_int_equal(ctx.security.level, 0x01u); /* still unlocked */
}

/*
 * Case C (default S3): s3_ms == 0 in config; stack must resolve to 5000 ms.
 * Advance time by 5001 ms; assert session reverts.
 */
static void test_s3_default_timeout_reverts_session(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_s3(&ctx, &cfg, 0u); /* 0 = use default */

    /* Resolved value must equal the documented default */
    assert_int_equal(ctx.session.s3_ms, 5000u);

    ctx.session.active = 0x03u;
    ctx.security.level = 0x01u;
    ctx.session.last_msg_time = 1000u;

    /* 5001 ms elapsed — default S3 expires */
    will_return(mock_get_time, 6001u);
    uds_process(&ctx);

    assert_int_equal(ctx.session.active, 0x01u); /* default session */
    assert_int_equal(ctx.security.level, 0x00u); /* relocked */
}

/*
 * Case D (default S3, not expired): 4999 ms elapsed; session must NOT revert.
 */
static void test_s3_default_timeout_not_yet_expired(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    init_ctx_s3(&ctx, &cfg, 0u);

    ctx.session.active = 0x03u;
    ctx.security.level = 0x01u;
    ctx.session.last_msg_time = 1000u;

    will_return(mock_get_time, 5999u); /* 4999 ms — not yet expired */
    uds_process(&ctx);

    assert_int_equal(ctx.session.active, 0x03u); /* still extended */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_s3_custom_timeout_reverts_session),
        cmocka_unit_test(test_s3_custom_timeout_not_yet_expired),
        cmocka_unit_test(test_s3_default_timeout_reverts_session),
        cmocka_unit_test(test_s3_default_timeout_not_yet_expired),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
