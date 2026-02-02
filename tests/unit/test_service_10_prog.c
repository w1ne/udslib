#include "test_helpers.h"

static void test_programming_session_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t request[] = {0x10, 0x02};

    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x50 02 + P2/P2* */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x02);
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x02);
}

static void test_session_reset_security(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.security_level = 1;

    uint8_t request[] = {0x10, 0x01};

    will_return(mock_get_time, 2000); 
    will_return(mock_get_time, 2000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.active_session, 0x01);
    assert_int_equal(ctx.security_level, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_programming_session_success),
        cmocka_unit_test(test_session_reset_security),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
