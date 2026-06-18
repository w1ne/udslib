/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static int mock_file_transfer(uds_ctx_t *ctx, uint8_t mode, const uint8_t *path, uint16_t path_len,
                              const uint8_t *params, uint16_t params_len, uint8_t *out_buf,
                              uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    assert_int_equal(mode, 0x01u); /* AddFile */
    assert_int_equal(path_len, 3u);
    assert_int_equal(path[0], 'a');
    assert_int_equal(path[1], 'b');
    assert_int_equal(path[2], 'c');
    assert_int_equal(params_len, 1u);
    assert_int_equal(params[0], 0x00u); /* dataFormatIdentifier */
    out_buf[0] = 0x02;                  /* lengthFormatIdentifier */
    out_buf[1] = 0x00;
    out_buf[2] = 0x80; /* maxNumberOfBlockLength */
    return 3;
}

static void test_file_transfer_ok(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_file_transfer = mock_file_transfer;

    /* 38 01 <pathLen=0x0003> 'a' 'b' 'c' <dataFormatId=0x00> */
    uint8_t req[] = {0x38, 0x01, 0x00, 0x03, 'a', 'b', 'c', 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 78 01 + [02 00 80] = 2 + 3 = 5 */
    expect_value(mock_tp_send, len, 5);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 8);

    assert_int_equal(g_tx_buf[0], 0x78);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_tx_buf[2], 0x02);
    assert_int_equal(g_tx_buf[4], 0x80);
}

static void test_file_transfer_bad_mode(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_file_transfer = mock_file_transfer;

    /* mode 0x06 is out of range -> NRC 0x31. */
    uint8_t req[] = {0x38, 0x06, 0x00, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x31);
}

static void test_file_transfer_path_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_file_transfer = mock_file_transfer;

    /* pathLen 0x0010 but only 1 path byte present -> NRC 0x13. */
    uint8_t req[] = {0x38, 0x01, 0x00, 0x10, 'a'};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_file_transfer_no_hook(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* No handler -> NRC 0x22. */
    uint8_t req[] = {0x38, 0x01, 0x00, 0x01, 'a'};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[2], 0x22);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_file_transfer_ok),
        cmocka_unit_test(test_file_transfer_bad_mode),
        cmocka_unit_test(test_file_transfer_path_overflow),
        cmocka_unit_test(test_file_transfer_no_hook),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
