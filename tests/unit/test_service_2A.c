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
    assert_int_equal(ctx.server.periodic_count, 1);
    assert_int_equal(ctx.server.periodic_ids[0], 0xE1);
    assert_int_equal(ctx.server.periodic_rates[0], 0x01);
}

static void test_periodic_scheduler_trigger(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read;

    /* Manually setup state */
    ctx.server.periodic_ids[0] = 0xE1;
    ctx.server.periodic_rates[0] = 0x01; /* Fast: 100ms */
    ctx.server.periodic_timers[0] = 1000;
    ctx.server.periodic_count = 1;

    /* Trigger scheduler at T=1000 */
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* E1 11 22 (Periodic messages use ID as header) */
    will_return(mock_tp_send, 0);

    uds_process(&ctx);

    /* Check timer reset */
    assert_int_equal(ctx.server.periodic_timers[0], 1100);
}

/* The scheduler deadline comparison must survive a 32-bit millisecond wrap:
   a deadline set just before rollover is still "due" once the clock wraps. */
static void test_periodic_scheduler_wraparound(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read;

    ctx.server.periodic_ids[0] = 0xE1;
    ctx.server.periodic_rates[0] = 0x01;         /* Fast: 100ms */
    ctx.server.periodic_timers[0] = 0xFFFFFF00u; /* deadline just before wrap */
    ctx.server.periodic_count = 1;

    /* Clock has wrapped past the deadline. */
    will_return(mock_get_time, 0x00000100u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* E1 11 22 */
    will_return(mock_tp_send, 0);

    uds_process(&ctx);

    /* Fired despite the wrap; next deadline = now + 100ms. */
    assert_int_equal(ctx.server.periodic_timers[0], 0x00000100u + 100u);
}

static void test_periodic_read_stop(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    ctx.server.periodic_ids[0] = 0xE1;
    ctx.server.periodic_count = 1;

    /* 2A 04 E1 (Stop ID 0xE1) */
    uint8_t req[] = {0x2A, 0x04, 0xE1};

    /* 2 calls confirmed by failure analysis */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1); /* 6A */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(ctx.server.periodic_count, 0);
    assert_int_equal(ctx.server.periodic_ids[0], 0x00);
}

/* Registering a periodic read with no fn_periodic_read configured must be
   rejected (0x22) -- otherwise the scheduler would later call a NULL pointer. */
static void test_periodic_read_requires_callback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* cfg.fn_periodic_read deliberately left NULL */

    uint8_t req[] = {0x2A, 0x01, 0xE1};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 2A 22 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[2], 0x22);
    assert_int_equal(ctx.server.periodic_count, 0);
}

/* Periodic read mock that returns UDS_MAX_PERIODIC_MSG_LEN (8) bytes. */
static int mock_periodic_read_full(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                                   uint16_t max_len)
{
    (void) ctx;
    (void) periodic_id;
    (void) max_len;
    /* Fill 8 bytes: encoded frame = id(1) + payload(8) = 9 bytes total. */
    memset(out_buf, 0x5A, 8);
    return 8;
}

/* §2d: periodic message whose encoded frame (id + payload) does not fit the
 * tx_buffer must be silently skipped — no memcpy, no fn_tp_send call.
 *
 * tx_buffer_size == 8 (the minimum accepted by uds_init); payload == 8 bytes
 * -> encoded frame = 1 + 8 = 9 bytes which exceeds the 8-byte buffer. */
static void test_periodic_scheduler_overflow_skipped(void **state)
{
    (void) state;

    static uint8_t small_tx[8]; /* minimum size accepted by uds_init */
    static uint8_t rx_buf[64];

    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = small_tx;
    cfg.tx_buffer_size = sizeof(small_tx); /* 8 bytes; 9-byte frame won't fit */
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.fn_periodic_read = mock_periodic_read_full;
    uds_init(&ctx, &cfg);

    /* Prime scheduler: ID 0xE2 fires at T=1000. */
    ctx.server.periodic_ids[0] = 0xE2;
    ctx.server.periodic_rates[0] = 0x01; /* Fast: 100ms */
    ctx.server.periodic_timers[0] = 1000;
    ctx.server.periodic_count = 1;

    /* Tick at T=1000: deadline met, but encoded frame (9 bytes) > tx_buffer_size (8).
     * No fn_tp_send expectation: the frame must be silently skipped. */
    will_return(mock_get_time, 1000);
    uds_process(&ctx);

    /* Timer still advances so the scheduler does not immediately re-fire. */
    assert_int_equal(ctx.server.periodic_timers[0], 1100u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_periodic_read_setup),
        cmocka_unit_test(test_periodic_read_requires_callback),
        cmocka_unit_test(test_periodic_scheduler_trigger),
        cmocka_unit_test(test_periodic_scheduler_wraparound),
        cmocka_unit_test(test_periodic_read_stop),
        cmocka_unit_test(test_periodic_scheduler_overflow_skipped),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
