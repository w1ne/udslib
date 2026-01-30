/**
 * @file test_service_28.c
 * @brief Unit tests for SID 0x28 (Communication Control)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static void test_comm_control_disable_rx_tx_success(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x28, 0x03};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x68);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(ctx.comm_state, 0x03);
}

static void test_comm_control_suppress_response(void **state)
{
    (void)state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x28, 0x80}; /* Enable RX/TX | Suppress Bit */

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* No tp_send expected */

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.comm_state, 0x00);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_comm_control_disable_rx_tx_success),
        cmocka_unit_test(test_comm_control_suppress_response),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
