/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int mock_periodic_read(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                              uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    if (periodic_id == 0xE1) {
        out_buf[0] = 0x11;
        out_buf[1] = 0x22;
        return 2;
    }
    return -1;
}

static void test_periodic_read_setup(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read;

    /* 2A 01 E1 (Fast Rate for ID 0xE1) */
    uint8_t req[] = {0x2A, 0x01, 0xE1};

    /* 3 calls confirmed by failure analysis:
       1. uds_input_sdu (last_msg_time)
       2. uds_internal_handle_periodic_read (periodic_timers)
       3. ??? (Possibly execute_handler or send_response logic)
    */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 6A */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(ctx.periodic_count, 1);
    assert_int_equal(ctx.periodic_ids[0], 0xE1);
    assert_int_equal(ctx.periodic_rates[0], 0x01);
}

static void test_periodic_scheduler_trigger(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read;

    /* Manually setup state */
    ctx.periodic_ids[0] = 0xE1;
    ctx.periodic_rates[0] = 0x01; /* Fast: 100ms */
    ctx.periodic_timers[0] = 1000;
    ctx.periodic_count = 1;

    /* Trigger scheduler at T=1000 */
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* E1 11 22 (Periodic messages use ID as header) */
    will_return(mock_tp_send, 0);

    uds_process(&ctx);

    /* Check timer reset */
    assert_int_equal(ctx.periodic_timers[0], 1100);
}

static void test_periodic_read_stop(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    ctx.periodic_ids[0] = 0xE1;
    ctx.periodic_count = 1;

    /* 2A 04 E1 (Stop ID 0xE1) */
    uint8_t req[] = {0x2A, 0x04, 0xE1};

    /* 2 calls confirmed by failure analysis */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 6A */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(ctx.periodic_count, 0);
    assert_int_equal(ctx.periodic_ids[0], 0x00);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_periodic_read_setup),
        cmocka_unit_test(test_periodic_scheduler_trigger),
        cmocka_unit_test(test_periodic_read_stop),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
