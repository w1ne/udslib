/**
 * @file test_core.c
 * @brief Unit tests for LibUDS Core Logic
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"

/* --- Mock Buffers --- */
static uint8_t g_tx_buf[1024];
static uint8_t g_rx_buf[1024];

/* --- Mocks --- */

static uint32_t mock_get_time(void)
{
    return (uint32_t)mock();
}

static int mock_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    check_expected_ptr(data);
    check_expected(len);
    memcpy(g_tx_buf, data, len);
    return (int)mock();
}

/* --- Test Setup --- */

static void setup_ctx(uds_ctx_t *ctx, uds_config_t *cfg)
{
    memset(cfg, 0, sizeof(uds_config_t));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_rx_buf;
    cfg->rx_buffer_size = sizeof(g_rx_buf);
    cfg->tx_buffer = g_tx_buf;
    cfg->tx_buffer_size = sizeof(g_tx_buf);

    uds_init(ctx, cfg);
}

/* --- Tests --- */

static void test_uds_init_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uint8_t tx_buf[1024];
    uds_config_t cfg = {.get_time_ms = mock_get_time,
                        .fn_tp_send = mock_tp_send,
                        .rx_buffer = g_rx_buf,
                        .rx_buffer_size = 1024,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 1024};
    assert_int_equal(uds_init(&ctx, &cfg), UDS_OK);
}

static void test_uds_init_fail(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    assert_int_equal(uds_init(NULL, NULL), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_init(&ctx, NULL), UDS_ERR_INVALID_ARG);
}

static void test_invalid_sid_nrc(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0xFE};

    /* Expectations */
    will_return(mock_get_time, 1000); /* For input handler */
    will_return(mock_get_time, 1000); /* For dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0xFE);
    assert_int_equal(g_tx_buf[2], 0x11);
}

static int custom_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    ctx->config->tx_buffer[0] = 0xAA;
    return uds_send_response(ctx, 1);
}

static void test_custom_service_registration(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uint8_t tx_buf[10];
    uds_service_entry_t user_services[] = {{0x66, 1, UDS_SESSION_ALL, 0, custom_handler}};
    uds_config_t cfg = {.get_time_ms = mock_get_time,
                        .fn_tp_send = mock_tp_send,
                        .rx_buffer = g_rx_buf,
                        .rx_buffer_size = 10,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 10,
                        .user_services = user_services,
                        .user_service_count = 1,
                        .p2_ms = 50,
                        .p2_star_ms = 5000};
    uds_init(&ctx, &cfg);

    uint8_t req[] = {0x66};
    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_value(mock_tp_send, data, tx_buf);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 1);
    assert_int_equal(tx_buf[0], 0xAA);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_uds_init_success),
        cmocka_unit_test(test_uds_init_fail),
        cmocka_unit_test(test_invalid_sid_nrc),
        cmocka_unit_test(test_custom_service_registration),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
