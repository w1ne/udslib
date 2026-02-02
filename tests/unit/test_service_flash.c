/**
 * @file test_service_flash.c
 * @brief Unit tests for Flash Engine Services (0x31, 0x34, 0x36, 0x37)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int g_routine_type = 0;
static uint16_t g_routine_id = 0;
static uint32_t g_download_addr = 0;
static uint32_t g_download_size = 0;
static uint8_t g_transfer_seq = 0;
static int g_transfer_exit_called = 0;

static int mock_routine_control(struct uds_ctx *ctx, uint8_t type, uint16_t id, const uint8_t *data, uint16_t len, uint8_t *out_buf, uint16_t max_len) {
    (void)ctx; (void)data; (void)len; (void)max_len;
    g_routine_type = type;
    g_routine_id = id;
    if (id == 0xFF00) { /* Erase Memory */
        out_buf[0] = 0x00; /* Success status */
        return 1;
    }
    return -0x31; /* Request Out Of Range */
}

static int mock_request_download(struct uds_ctx *ctx, uint32_t addr, uint32_t size) {
    (void)ctx;
    g_download_addr = addr;
    g_download_size = size;
    return UDS_OK;
}

static int mock_transfer_data(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data, uint16_t len) {
    (void)ctx; (void)data; (void)len;
    g_transfer_seq = sequence;
    return UDS_OK;
}

static int mock_transfer_exit(struct uds_ctx *ctx) {
    (void)ctx;
    g_transfer_exit_called++;
    return UDS_OK;
}

static void test_routine_control_erase_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_routine_control = mock_routine_control;

    uint8_t req[] = {0x31, 0x01, 0xFF, 0x00}; /* Start Erase Memory */
    
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 5); /* 0x71 01 FF 00 00 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 4)ctx, req, 4, 0);
    assert_int_equal(g_tx_buf[0], 0x71);
    assert_int_equal(g_routine_id, 0xFF00);
}

static void test_request_download_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = mock_request_download;

    /* format 0x00, addr 0x11223344, size 0x00001000 */
    uint8_t req[] = {0x34, 0x00, 0x44, 0x11, 0x22, 0x33, 0x44, 0x00, 0x00, 0x10, 0x00}; 
    
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 0x74 20 04 00 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 11)ctx, req, 11, 0);
    assert_int_equal(g_tx_buf[0], 0x74);
    assert_int_equal(g_download_addr, 0x11223344);
    assert_int_equal(g_download_size, 0x00001000);
}

static void test_transfer_data_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data;

    uint8_t req[] = {0x36, 0x01, 0xDE, 0xAD, 0xBE, 0xEF}; 
    
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x76 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 6)ctx, req, 6, 0);
    assert_int_equal(g_tx_buf[0], 0x76);
    assert_int_equal(g_transfer_seq, 0x01);
}

static void test_transfer_exit_success(void **state) {
    (void)state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_exit = mock_transfer_exit;
    g_transfer_exit_called = 0;

    uint8_t req[] = {0x37};
    
    will_return(mock_get_time, 1000); 
    will_return(mock_get_time, 1000); 
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 0x77 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&ctx, req, 1)ctx, req, 1, 0);
    assert_int_equal(g_tx_buf[0], 0x77);
    assert_int_equal(g_transfer_exit_called, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_routine_control_erase_success),
        cmocka_unit_test(test_request_download_success),
        cmocka_unit_test(test_transfer_data_success),
        cmocka_unit_test(test_transfer_exit_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
