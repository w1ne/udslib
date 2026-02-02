/**
 * @file test_service_dtc.c
 * @brief Unit tests for DTC Services (0x14, 0x19, 0x85)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int g_dtc_clear_count = 0;
static uint32_t g_last_clear_group = 0;

static int mock_dtc_clear(struct uds_ctx *ctx, uint32_t group) {
    (void)ctx;
    g_dtc_clear_count++;
    g_last_clear_group = group;
    return UDS_OK;
}

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len) {
    (void)ctx;
    (void)max_len;
    if (subfn == 0x01) { /* count of DTCs matching status mask */
        out_buf[0] = 0x01; /* availability mask */
        out_buf[1] = 0x00; /* status availability */
        out_buf[2] = 0x00; /* count MSB */
        out_buf[3] = 0x01; /* count LSB */
        return 4;
    }
    return -0x31; /* Request Out Of Range */
}

static void test_clear_dtc_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_clear = mock_dtc_clear;
    g_dtc_clear_count = 0;

    uint8_t req[] = {0x14, 0xAA, 0xBB, 0xCC}; /* Clear Group AABBCC */
    
    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 0x54 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 4)ctx, req, 4, 0);
    assert_int_equal(g_tx_buf[0], 0x54);
    assert_int_equal(g_dtc_clear_count, 1);
    assert_int_equal(g_last_clear_group, 0xAABBCC);
}

static void test_read_dtc_info_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read;

    uint8_t req[] = {0x19, 0x01, 0xFF}; /* Read number of DTCs by status mask 0xFF */
    
    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x59 01 status msb lsb ... */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 3)ctx, req, 3, 0);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_tx_buf[5], 0x01); /* Count LSB */
}

static void test_control_dtc_setting_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t req[] = {0x85, 0x01}; /* DTC Setting ON */
    
    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0xC5 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 2)ctx, req, 2, 0);
    assert_int_equal(g_tx_buf[0], 0xC5);
    assert_int_equal(g_tx_buf[1], 0x01);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_clear_dtc_success),
        cmocka_unit_test(test_read_dtc_info_success),
        cmocka_unit_test(test_control_dtc_setting_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
