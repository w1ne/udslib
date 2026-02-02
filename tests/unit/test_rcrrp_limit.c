#include "test_helpers.h"

static int mock_pending_handler(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    (void)ctx; (void)data; (void)len;
    return UDS_PENDING;
}

static void test_core_rcrrp_limit(void **state) {
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    uint8_t rx_buf[256];
    uint8_t tx_buf[256];
    
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 100;
    cfg.rcrrp_limit = 2;

    static const uds_service_entry_t services[] = {
        {0x99, 1, UDS_SESSION_ALL, 0, mock_pending_handler, NULL}
    };
    cfg.user_services = services;
    cfg.user_service_count = 1;

    uds_init(&ctx, &cfg);

    uint8_t req[] = {0x99};
    
    /* 1. Request starts. 
       - ads_input_sdu calls get_time_ms twice.
       - execute_handler calls get_time_ms once.
       - execute_handler calls uds_send_nrc(0x78) immediately.
    */
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 1);
    
    assert_true(ctx.p2_msg_pending);
    assert_int_equal(ctx.rcrrp_count, 0);
    assert_int_equal(g_tx_buf[2], 0x78);

    /* 2. T+110ms: Exceed P2* (100ms). Should send 1st REPEATED NRC 0x78 */
    will_return(mock_get_time, 1110); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    assert_int_equal(g_tx_buf[2], 0x78);
    assert_int_equal(ctx.rcrrp_count, 1);

    /* 3. T+220ms: Exceed P2* (100ms). Should send 2nd REPEATED NRC 0x78 */
    will_return(mock_get_time, 1220); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    assert_int_equal(g_tx_buf[2], 0x78);
    assert_int_equal(ctx.rcrrp_count, 2);

    /* 4. T+330ms: Exceed P2* again. Limit is 2. Should send NRC 0x22 and clear pending. */
    will_return(mock_get_time, 1330); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    assert_int_equal(g_tx_buf[2], 0x22); /* ConditionsNotCorrect */
    assert_false(ctx.p2_msg_pending);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_core_rcrrp_limit),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
