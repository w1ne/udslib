#include "test_helpers.h"

static void test_p2_timeout_nrc78(void **state) {
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.p2_ms = 50;

    /* Start a pending service (like 0x31 mock we added) */
    uint8_t request[] = {0x31};
    
    will_return(mock_get_time, 1000); // Input
    will_return(mock_get_time, 1000); // Dispatch
    uds_input_sdu(&ctx, request, sizeof(request));

    assert_true(ctx.p2_msg_pending);
    assert_int_equal(ctx.pending_sid, 0x31);

    /* Move time forward by 51ms */
    will_return(mock_get_time, 1051);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); // 7F 31 78
    will_return(mock_tp_send, 0);

    uds_process(&ctx);

    assert_true(ctx.p2_star_active);
    assert_int_equal(g_tx_buf[2], 0x78);
}

static void test_s3_timeout_reset(void **state) {
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    ctx.active_session = 0x03;
    ctx.last_msg_time = 1000;

    /* Move time forward by 5001ms */
    will_return(mock_get_time, 6001);
    
    uds_process(&ctx);

    assert_int_equal(ctx.active_session, 0x01); // Back to default
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_p2_timeout_nrc78),
        cmocka_unit_test(test_s3_timeout_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
