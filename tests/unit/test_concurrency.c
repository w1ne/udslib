#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Mock Transport Send */
static int mock_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    check_expected_ptr(data);
    check_expected(len);
    return 0;
}

static uint32_t mock_time = 0;
static uint32_t mock_get_time(void)
{
    return mock_time;
}

/* Async Handler: returns UDS_PENDING */
static int async_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return UDS_PENDING;
}

/* 1. Verify Busy Rejection (NRC 0x21) */
static void test_concurrent_request_rejection(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    uds_service_entry_t user_services[] = {{0x31, 2, UDS_SESSION_ALL, 0, async_handler, NULL}};

    uds_config_t cfg = {.fn_tp_send = mock_tp_send,
                        .rx_buffer = rx_buf,
                        .rx_buffer_size = 64,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 64,
                        .user_services = user_services,
                        .user_service_count = 1,
                        .get_time_ms = mock_get_time,
                        .p2_ms = 100,
                        .p2_star_ms = 1000};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    /* 1. Send first request. Expect NRC 0x78 (Pending) */
    uint8_t req1[] = {0x31, 0x01};
    uint8_t exp_nrc78_req1[] = {0x7F, 0x31, 0x78};
    expect_memory(mock_tp_send, data, exp_nrc78_req1, 3);
    expect_value(mock_tp_send, len, 3);

    mock_time = 1000;
    uds_input_sdu(&ctx, req1, 2);
    assert_true(ctx.p2_msg_pending);
    assert_int_equal(ctx.pending_sid, 0x31);

    /* 2. Send second request while first is still pending. */
    /* Expect NRC 0x21 (Busy) */
    uint8_t req2[] = {0x22, 0xF1, 0x90};
    uint8_t exp_nrc21_req2[] = {0x7F, 0x22, 0x21};
    expect_memory(mock_tp_send, data, exp_nrc21_req2, 3);
    expect_value(mock_tp_send, len, 3);

    mock_time = 1050;
    uds_input_sdu(&ctx, req2, 3);

    /* 3. Verify first operation is still alive/pending */
    assert_true(ctx.p2_msg_pending);
    assert_int_equal(ctx.pending_sid, 0x31);
    assert_int_equal(ctx.p2_timer_start,
                     1000); /* Timer should NOT have been reset by the rejected request */

    /* 4. Complete first request */
    uint8_t exp_pos_req1[] = {0x71, 0x01};
    expect_memory(mock_tp_send, data, exp_pos_req1, 2);
    expect_value(mock_tp_send, len, 2);

    ctx.config->tx_buffer[0] = 0x71;
    ctx.config->tx_buffer[1] = 0x01;
    uds_send_response(&ctx, 2);
    assert_false(ctx.p2_msg_pending);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_concurrent_request_rejection),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
