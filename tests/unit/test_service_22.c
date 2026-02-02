/**
 * @file test_service_22.c
 * @brief Unit tests for SID 0x22 (Read Data By Identifier)
 */

#include "test_helpers.h"

static uint8_t g_vin[] = "LIBUDS_SIM_001";

static const uds_did_entry_t g_test_dids[] = {
    {0xF190, 14, NULL, NULL, g_vin},
};

static const uds_did_table_t g_test_table = {
    .entries = g_test_dids,
    .count = 1
};

static void test_rdbi_vin_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0xF1, 0x90};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3 + 14); /* 0x62 F1 90 + VIN(14) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_memory_equal(&g_tx_buf[3], g_vin, 14);
}

static void test_rdbi_unsupported_id_nrc(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x22, 0x12, 0x34};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x31); /* Request Out of Range */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_vin_success),
        cmocka_unit_test(test_rdbi_unsupported_id_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
