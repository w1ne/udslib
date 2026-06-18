/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static int mock_read_scaling(uds_ctx_t *ctx, uint16_t did, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    assert_int_equal(did, 0xF190u);
    out_buf[0] = 0x91; /* scalingByte: type 9 (formula), length 1 */
    out_buf[1] = 0x01; /* scalingData */
    return 2;
}

static void test_read_scaling_ok(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_read_scaling = mock_read_scaling;

    /* 0x24 <DID=F1 90> */
    uint8_t req[] = {0x24, 0xF1, 0x90};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 64 F1 90 + [91 01] = 3 + 2 = 5 */
    expect_value(mock_tp_send, len, 5);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[0], 0x64);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_int_equal(g_tx_buf[3], 0x91);
    assert_int_equal(g_tx_buf[4], 0x01);
}

static void test_read_scaling_no_hook(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* No fn_read_scaling -> NRC 0x31 (requestOutOfRange: DID not supported). */
    uint8_t req[] = {0x24, 0xF1, 0x90};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x24);
    assert_int_equal(g_tx_buf[2], 0x31);
}

static void test_read_scaling_hook_nrc(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_read_scaling = NULL;
    /* Short request: SID + 1 byte DID -> length error 0x13. */
    uint8_t req[] = {0x24, 0xF1};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x13);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_scaling_ok),
        cmocka_unit_test(test_read_scaling_no_hook),
        cmocka_unit_test(test_read_scaling_hook_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
