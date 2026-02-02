/**
 * @file test_service_27.c
 * @brief Unit tests for SID 0x27 (Security Access)
 */

#include "test_helpers.h"

static void test_security_access_seed(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x27, 0x01};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x67 01 + Seed(4) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x01);
}

static void test_security_access_key_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    /** Mock Key based on Seed DE AD BE EF is DF AE BF F0 */
    uint8_t request[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};

    will_return(mock_get_time, 2000); /* Input */
    will_return(mock_get_time, 2000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x67 02 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(ctx.security_level, 1);
    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x02);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_security_access_seed),
        cmocka_unit_test(test_security_access_key_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
