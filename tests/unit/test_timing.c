/**
 * @file test_timing.c
 * @brief Unit tests for LibUDS Timing Engine (P2/P2* and S3)
 */

#include "test_helpers.h"

static void test_p2_timeout_nrc78(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.p2_ms = 50;

    /* Manually trigger pending state for timeout testing */
    ctx.p2_msg_pending = true;
    ctx.pending_sid = 0x31;
    ctx.last_msg_time = 1000;

    /* Move time forward by 51ms */
    will_return(mock_get_time, 1051);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 31 78 */
    will_return(mock_tp_send, 0);

    uds_process(&ctx);

    assert_true(ctx.p2_star_active);
    assert_int_equal(g_tx_buf[2], 0x78);
}

static void test_s3_timeout_reset(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    ctx.active_session = 0x03;
    ctx.last_msg_time = 1000;

    /* Move time forward by 5001ms (S3 default is 5000ms) */
    will_return(mock_get_time, 6001);

    uds_process(&ctx);

    assert_int_equal(ctx.active_session, 0x01); /* Back to default */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_p2_timeout_nrc78),
        cmocka_unit_test(test_s3_timeout_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
