/**
 * @file test_service_10.c
 * @brief Unit tests for SID 0x10 (Diagnostic Session Control)
 */

#include "test_helpers.h"

static void test_extended_session_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x10, 0x03};

    will_return(mock_get_time, 1000); /* Input handler */
    will_return(mock_get_time, 1000); /* Dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x50 03 P2 P2 P2* P2* */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x03);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x03);
}

static void test_default_session_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    ctx.active_session = 0x03;

    uint8_t request[] = {0x10, 0x01};

    will_return(mock_get_time, 2000); /* Input */
    will_return(mock_get_time, 2000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x01);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x01);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_extended_session_success),
        cmocka_unit_test(test_default_session_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
