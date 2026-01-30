#include "test_helpers.h"

static void test_rdbi_vin_success(void **state) {
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x22, 0xF1, 0x90};
    
    will_return(mock_get_time, 1000); // Input
    will_return(mock_get_time, 1000); // Dispatch
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 14); // 0x62 F1 90 + VIN(14)
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_memory_equal(&g_tx_buf[3], "LIBUDS_SIM_001", 14);
}

static void test_rdbi_unsupported_id_nrc(void **state) {
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x22, 0x12, 0x34};
    
    will_return(mock_get_time, 1000); // Input
    will_return(mock_get_time, 1000); // Dispatch
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); 
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x31); // Request Out of Range
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_vin_success),
        cmocka_unit_test(test_rdbi_unsupported_id_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
