/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_init_validation.c
 * @brief Unit tests for uds_init() buffer-size validation (§2b hardening)
 *
 * Verifies that uds_init() rejects configs with undersized TX or RX buffers
 * and accepts a correctly-sized config.
 */

#include "test_helpers.h"
#include "uds/uds_core.h"

static uint8_t g_small_buf[4];
static uint8_t g_valid_buf[8];

static void make_valid_cfg(uds_config_t *cfg)
{
    memset(cfg, 0, sizeof(uds_config_t));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_valid_buf;
    cfg->rx_buffer_size = sizeof(g_valid_buf); /* 8 — exactly at floor */
    cfg->tx_buffer = g_valid_buf;
    cfg->tx_buffer_size = sizeof(g_valid_buf); /* 8 — exactly at floor */
}

static void test_valid_config_inits_ok(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    make_valid_cfg(&cfg);

    int rc = uds_init(&ctx, &cfg);
    assert_int_equal(rc, UDS_OK);
}

static void test_small_tx_buffer_rejected(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    make_valid_cfg(&cfg);
    cfg.tx_buffer = g_small_buf;
    cfg.tx_buffer_size = 4u; /* below UDS_MIN_TX_BUFFER_SIZE */

    int rc = uds_init(&ctx, &cfg);
    assert_int_equal(rc, UDS_ERR_INVALID_ARG);
}

static void test_small_rx_buffer_rejected(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    make_valid_cfg(&cfg);
    cfg.rx_buffer = g_small_buf;
    cfg.rx_buffer_size = 4u; /* below UDS_MIN_RX_BUFFER_SIZE */

    int rc = uds_init(&ctx, &cfg);
    assert_int_equal(rc, UDS_ERR_INVALID_ARG);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_valid_config_inits_ok),
        cmocka_unit_test(test_small_tx_buffer_rejected),
        cmocka_unit_test(test_small_rx_buffer_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
