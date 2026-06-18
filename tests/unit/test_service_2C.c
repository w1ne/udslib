/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static uint8_t g_last_subfn;
static uint16_t g_last_did;

static int mock_dynamic_did(uds_ctx_t *ctx, uint8_t subfn, uint16_t defined_did,
                            const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    g_last_subfn = subfn;
    g_last_did = defined_did;
    return 0;
}

static void test_dynamic_define_by_id(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = mock_dynamic_did;

    /* 2C 01 <definedDID=F2 00> <sourceDID=F1 90> <pos=01> <size=04> */
    uint8_t req[] = {0x2C, 0x01, 0xF2, 0x00, 0xF1, 0x90, 0x01, 0x04};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 6C 01 F2 00 = 4 */
    expect_value(mock_tp_send, len, 4);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 8);

    assert_int_equal(g_tx_buf[0], 0x6C);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_tx_buf[2], 0xF2);
    assert_int_equal(g_tx_buf[3], 0x00);
    assert_int_equal(g_last_subfn, 0x01);
    assert_int_equal(g_last_did, 0xF200);
}

static void test_dynamic_clear_all(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = mock_dynamic_did;

    /* 2C 03 : clear all dynamically defined DIDs (no DID). */
    uint8_t req[] = {0x2C, 0x03};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 6C 03 = 2 */
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_tx_buf[0], 0x6C);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_last_did, 0x0000);
}

static void test_dynamic_define_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = mock_dynamic_did;

    /* 0x01 requires a definedDID (2 bytes): len < 4 -> NRC 0x13. */
    uint8_t req[] = {0x2C, 0x01, 0xF2};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_dynamic_bad_subfn(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = mock_dynamic_did;

    /* sub-function 0x04 not supported -> NRC 0x12. */
    uint8_t req[] = {0x2C, 0x04, 0xF2, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x12);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dynamic_define_by_id),
        cmocka_unit_test(test_dynamic_clear_all),
        cmocka_unit_test(test_dynamic_define_short),
        cmocka_unit_test(test_dynamic_bad_subfn),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
