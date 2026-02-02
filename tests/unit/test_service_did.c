/**
 * @file test_service_did.c
 * @brief Unit tests for Table-Driven DID Registry (0x22/0x2E)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static uint8_t g_val_8 = 0x11;
static char g_str[10] = "OLD";

static int mock_did_read_fn(uds_ctx_t *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void)ctx;
    (void)max_len;
    if (did == 0x0100) {
        buf[0] = 0xAA;
        return 0;
    }
    return -1;
}

static int mock_did_write_fn(uds_ctx_t *ctx, uint16_t did, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (did == 0x0200 && len == 3) {
        memcpy(g_str, data, 3);
        return 0;
    }
    return -1;
}

static const uds_did_entry_t g_test_dids[] = {
    {0xF190, 8, NULL, NULL, &g_val_8},       /* Direct Storage */
    {0x0100, 1, mock_did_read_fn, NULL, NULL}, /* Read Callback */
    {0x0200, 3, NULL, mock_did_write_fn, NULL},/* Write Callback */
};

static const uds_did_table_t g_test_table = {
    .entries = g_test_dids,
    .count = 3
};

static void test_rdbi_single_did_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0xF1, 0x90};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 11); /* 0x62 + F1 90 + 8 bytes */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_int_equal(g_tx_buf[3], 0x11);
}

static void test_rdbi_callback_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0x01, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[3], 0xAA);
}

static void test_wdbi_callback_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;
    strcpy(g_str, "OLD");

    uint8_t request[] = {0x2E, 0x02, 0x00, 'N', 'E', 'W'};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x6E);
    assert_string_equal(g_str, "NEW");
}

static void test_rdbi_invalid_did_nrc(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0x99, 0x99};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, request, sizeof(request)ctx, request, sizeof(request, 0));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x31);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_single_did_success),
        cmocka_unit_test(test_rdbi_callback_success),
        cmocka_unit_test(test_wdbi_callback_success),
        cmocka_unit_test(test_rdbi_invalid_did_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
