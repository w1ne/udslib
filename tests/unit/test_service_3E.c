/**
 * @file test_service_3E.c
 * @brief Unit tests for SID 0x3E (Tester Present)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static void test_tester_present_zero_sub(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x3E, 0x00};

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7E);
    assert_int_equal(g_tx_buf[1], 0x00);
}

static void test_tester_present_suppress_bit(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    uint8_t request[] = {0x3E, 0x80}; /* 0x00 | 0x80 (Suppress Pos Response) */

    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* No expected tp_send! */

    uds_input_sdu(&ctx, request, sizeof(request));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_tester_present_zero_sub),
        cmocka_unit_test(test_tester_present_suppress_bit),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
