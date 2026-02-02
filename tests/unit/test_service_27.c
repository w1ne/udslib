#include "test_helpers.h"

static int mock_security_seed(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len) {
    (void)ctx; (void)level; (void)max_len;
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

static int mock_security_key(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed, const uint8_t *key, uint16_t key_len) {
    (void)ctx; (void)level; (void)seed; (void)key; (void)key_len;
    if (key[0] == 0xDF && key[1] == 0xAE && key[2] == 0xBF && key[3] == 0xF0) {
        return 0;
    }
    return -0x35; /* Invalid Key */
}

static void test_security_access_seed(void **state)
{
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;

    uint8_t request[] = {0x27, 0x01};

    /* 3 calls to get_time_ms: 2 in uds_input_sdu, 1 in handler */
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x67 01 + Seed(4) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x01);
}

static void test_security_access_key_success(void **state)
{
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_key = mock_security_key;

    /** Mock Key based on Seed DE AD BE EF is DF AE BF F0 */
    uint8_t request[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};

    will_return(mock_get_time, 2000); 
    will_return(mock_get_time, 2000); 
    will_return(mock_get_time, 2000); 

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x67 02 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.security_level, 1);
    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x02);
}

static void test_security_access_delay_timer(void **state)
{
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_key = mock_security_key;
    cfg.security_max_attempts = 1;
    cfg.security_delay_ms = 1000;

    uint8_t request_fail[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
    uint8_t request_ok[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};

    /* 1. Fail first attempt */
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 36 (Exceeded attempts) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_fail, sizeof(request_fail));
    assert_int_equal(g_tx_buf[2], 0x36);
    assert_int_equal(ctx.security_delay_end, 2000);

    /* 2. Try again before delay expires */
    will_return(mock_get_time, 1500); 
    will_return(mock_get_time, 1500); 
    will_return(mock_get_time, 1500); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 37 (Required delay) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_ok, sizeof(request_ok));
    assert_int_equal(g_tx_buf[2], 0x37);

    /* 3. Try again after delay expires */
    will_return(mock_get_time, 2500); 
    will_return(mock_get_time, 2500); 
    will_return(mock_get_time, 2500); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x67 02 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_ok, sizeof(request_ok));
    assert_int_equal(g_tx_buf[0], 0x67);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_security_access_seed),
        cmocka_unit_test(test_security_access_key_success),
        cmocka_unit_test(test_security_access_delay_timer),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
