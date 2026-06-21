/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_22.c
 * @brief Unit tests for SID 0x22 (Read Data By Identifier)
 */

#include "test_helpers.h"

static uint8_t g_vin[] = "UDSLIB_SIM_001";

static const uds_did_entry_t g_test_dids[] = {
    {0xF190, 14, UDS_SESSION_ALL, 0, NULL, NULL, g_vin},
};

static const uds_did_table_t g_test_table = {.entries = g_test_dids, .count = 1};

/* --- Standardized identification DIDs (ISO 14229-1, issue #69) --- */
static uint8_t g_serial[] = "ECU-SN-0001-2026";         /* 0xF18C */
static uint8_t g_mfg_date[] = {0x20, 0x26, 0x06, 0x21}; /* 0xF18B BCD */

static int read_active_session(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) did;
    if (max_len < 1u) return -0x22;
    buf[0] = ctx->active_session;
    return 1;
}

static const uds_did_entry_t g_std_dids[] = {
    {0xF18B, 4, UDS_SESSION_ALL, 0, NULL, NULL, g_mfg_date},          /* binary (BCD) */
    {0xF18C, 16, UDS_SESSION_ALL, 0, NULL, NULL, g_serial},           /* ASCII storage */
    {0xF186, 1, UDS_SESSION_ALL, 0, read_active_session, NULL, NULL}, /* dynamic read */
};

static const uds_did_table_t g_std_table = {.entries = g_std_dids, .count = 3};

static void test_rdbi_vin_success(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0xF1, 0x90};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 14); /* 0x62 F1 90 + VIN(14) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_memory_equal(&g_tx_buf[3], g_vin, 14);
}

static void test_rdbi_unsupported_id_nrc(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x22, 0x12, 0x34};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x31); /* Request Out of Range */
}

static void test_rdbi_std_serial_ascii(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_std_table;

    uint8_t request[] = {0x22, 0xF1, 0x8C};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 16); /* 0x62 F1 8C + serial(16) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x8C);
    assert_memory_equal(&g_tx_buf[3], g_serial, 16);
}

static void test_rdbi_std_mfg_date_bcd(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_std_table;

    uint8_t request[] = {0x22, 0xF1, 0x8B};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 4); /* 0x62 F1 8B + date(4) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x8B);
    assert_memory_equal(&g_tx_buf[3], g_mfg_date, 4);
}

static void test_rdbi_std_active_session_dynamic(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_std_table;

    uint8_t request[] = {0x22, 0xF1, 0x86};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 1); /* 0x62 F1 86 + session(1) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x86);
    assert_int_equal(g_tx_buf[3], ctx.active_session); /* default session 0x01 */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_vin_success),
        cmocka_unit_test(test_rdbi_unsupported_id_nrc),
        cmocka_unit_test(test_rdbi_std_serial_ascii),
        cmocka_unit_test(test_rdbi_std_mfg_date_bcd),
        cmocka_unit_test(test_rdbi_std_active_session_dynamic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
