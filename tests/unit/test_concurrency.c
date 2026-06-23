/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

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
    (void) ctx;
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
static void async_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_pending(out);
}

/* 1. Verify Busy Rejection (NRC 0x21) */
static void test_concurrent_request_rejection(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    uds_service_entry_t user_services[] = {{0x31, 2, UDS_SESSION_ALL, 0, async_handler, NULL, 0u}};

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
    assert_true(ctx.server.p2_msg_pending);
    assert_int_equal(ctx.server.pending_sid, 0x31);

    /* 2. Send second request while first is still pending. */
    /* Expect NRC 0x21 (Busy) */
    uint8_t req2[] = {0x22, 0xF1, 0x90};
    uint8_t exp_nrc21_req2[] = {0x7F, 0x22, 0x21};
    expect_memory(mock_tp_send, data, exp_nrc21_req2, 3);
    expect_value(mock_tp_send, len, 3);

    mock_time = 1050;
    uds_input_sdu(&ctx, req2, 3);

    /* 3. Verify first operation is still alive/pending */
    assert_true(ctx.server.p2_msg_pending);
    assert_int_equal(ctx.server.pending_sid, 0x31);
    assert_int_equal(ctx.server.p2_timer_start,
                     1000); /* Timer should NOT have been reset by the rejected request */

    /* 4. Complete first request */
    uint8_t exp_pos_req1[] = {0x71, 0x01};
    expect_memory(mock_tp_send, data, exp_pos_req1, 2);
    expect_value(mock_tp_send, len, 2);

    ctx.config->tx_buffer[0] = 0x71;
    ctx.config->tx_buffer[1] = 0x01;
    uds_send_response(&ctx, 2);
    assert_false(ctx.server.p2_msg_pending);
}

/* --- I1: responsePending 0x78 must not be clobbered by a same-tick periodic --- */

/* Recording transport: capture every frame sent in a tick so we can assert the
 * 0x78 responsePending was transmitted alongside the periodic frame. */
#define REC_MAX 8
static uint8_t g_rec_frames[REC_MAX][64];
static uint16_t g_rec_lens[REC_MAX];
static int g_rec_count;

static int rec_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    if (g_rec_count < REC_MAX) {
        uint16_t n = (len <= 64u) ? len : 64u;
        memcpy(g_rec_frames[g_rec_count], data, n);
        g_rec_lens[g_rec_count] = len;
    }
    g_rec_count++;
    return 0;
}

static int rec_periodic_read(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
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

/* RED before the fix: when the P2* deadline expires on the SAME uds_process tick a
 * periodic (0x2A) is due, the staged responsePending 0x78 was overwritten by the
 * periodic frame in the shared tx_buffer and silently dropped. The tester must
 * still receive the 0x78. */
static void test_responsepending_not_clobbered_by_periodic(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    uds_service_entry_t user_services[] = {{0x31, 2, UDS_SESSION_ALL, 0, async_handler, NULL, 0u}};

    uds_config_t cfg = {.fn_tp_send = rec_tp_send,
                        .rx_buffer = rx_buf,
                        .rx_buffer_size = 64,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 64,
                        .user_services = user_services,
                        .user_service_count = 1,
                        .get_time_ms = mock_get_time,
                        .fn_periodic_read = rec_periodic_read,
                        .p2_ms = 100,
                        .p2_star_ms = 1000};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    /* 1. Start an async request -> handler returns UDS_PENDING -> initial 0x78. */
    g_rec_count = 0;
    uint8_t req[] = {0x31, 0x01};
    mock_time = 1000;
    uds_input_sdu(&ctx, req, 2);
    assert_true(ctx.server.p2_msg_pending);

    /* 2. Arm a periodic id that is due on the next tick. */
    ctx.server.periodic_ids[0] = 0xE1;
    ctx.server.periodic_rates[0] = 0x01; /* Fast: 100ms */
    ctx.server.periodic_timers[0] = 2000;
    ctx.server.periodic_count = 1;

    /* 3. Advance past the P2* deadline AND past the periodic deadline so BOTH fire
     * on the same uds_process tick. */
    g_rec_count = 0;
    mock_time = 2500;
    uint8_t rcrrp_before = ctx.server.rcrrp_count;
    uint32_t p2_start_before = ctx.server.p2_timer_start;
    uds_process(&ctx);

    /* The responsePending 0x78 must have been transmitted (not dropped), AND the
     * periodic 0xE1 frame too: two frames this tick. */
    assert_int_equal(g_rec_count, 2);

    int saw_78 = 0;
    int saw_periodic = 0;
    for (int i = 0; i < g_rec_count && i < REC_MAX; i++) {
        if (g_rec_lens[i] == 3u && g_rec_frames[i][0] == 0x7F && g_rec_frames[i][1] == 0x31 &&
            g_rec_frames[i][2] == 0x78) {
            saw_78 = 1;
        }
        if (g_rec_lens[i] == 3u && g_rec_frames[i][0] == 0xE1) {
            saw_periodic = 1;
        }
    }
    assert_true(saw_78);
    assert_true(saw_periodic);

    /* P2-star / RCRRP semantics preserved: rcrrp_count incremented, P2* re-armed. */
    assert_int_equal(ctx.server.rcrrp_count, rcrrp_before + 1u);
    assert_true(ctx.server.p2_star_active);
    assert_int_not_equal(ctx.server.p2_timer_start, p2_start_before);
}

/* --- I2: posttx flag bookkeeping must be serialized under the lock --- */

static int g_i2_lock_depth;
static int g_i2_reset_called;
static int g_i2_reset_under_lock; /* -1 = not observed */
static int g_i2_txcomplete_under_lock;

static void i2_lock(void *h)
{
    (void) h;
    g_i2_lock_depth++;
}
static void i2_unlock(void *h)
{
    (void) h;
    g_i2_lock_depth--;
}
static bool i2_tx_complete(struct uds_ctx *ctx)
{
    (void) ctx;
    g_i2_txcomplete_under_lock = (g_i2_lock_depth > 0) ? 1 : 0;
    return true;
}
static void i2_reset(struct uds_ctx *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    g_i2_reset_under_lock = (g_i2_lock_depth > 0) ? 1 : 0;
    g_i2_reset_called++;
}

/* The posttx flag test-and-clear must happen UNDER the lock, while the action
 * callback (fn_reset) and the transport-complete poll (fn_tx_complete) run with
 * the lock RELEASED. We observe lock depth at the moment each callback fires:
 * the flags are cleared inside the locked region the test can never directly see,
 * but we assert the callbacks themselves are NOT under the lock and the lock is
 * balanced to zero after the action runs. */
static void test_posttx_action_bookkeeping_lock_scope(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    uds_config_t cfg = {.fn_tp_send = rec_tp_send,
                        .rx_buffer = rx_buf,
                        .rx_buffer_size = 64,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 64,
                        .get_time_ms = mock_get_time,
                        .fn_mutex_lock = i2_lock,
                        .fn_mutex_unlock = i2_unlock,
                        .fn_reset = i2_reset,
                        .fn_tx_complete = i2_tx_complete,
                        .p2_ms = 100,
                        .p2_star_ms = 1000};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    g_i2_lock_depth = 0;
    g_i2_reset_called = 0;
    g_i2_reset_under_lock = -1;
    g_i2_txcomplete_under_lock = -1;
    g_rec_count = 0;

    /* ECUReset hard (0x11 0x01): response transmits and arms the deferred reset. */
    uint8_t reset_req[] = {0x11, 0x01};
    mock_time = 1000;
    uds_input_sdu(&ctx, reset_req, sizeof(reset_req));
    assert_int_equal(g_i2_reset_called, 0); /* deferred to uds_process */
    assert_int_equal(g_i2_lock_depth, 0);   /* lock balanced after dispatch */

    /* uds_process tick: drains the deferred reset. */
    mock_time = 1001;
    uds_process(&ctx);

    assert_int_equal(g_i2_reset_called, 1);
    /* The action callback and the transport-complete poll ran with the lock
     * RELEASED (a rebooting fn_reset must never hold the lock). */
    assert_int_equal(g_i2_reset_under_lock, 0);
    assert_int_equal(g_i2_txcomplete_under_lock, 0);
    /* Lock balanced to zero: every lock taken for the flag bookkeeping was
     * released (single-unlock-per-path discipline preserved). */
    assert_int_equal(g_i2_lock_depth, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_concurrent_request_rejection),
        cmocka_unit_test(test_responsepending_not_clobbered_by_periodic),
        cmocka_unit_test(test_posttx_action_bookkeeping_lock_scope),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
