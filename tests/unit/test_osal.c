/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include <string.h>
#include "uds/uds_core.h"
#include "uds/uds_config.h"

static int mock_can_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return 0;
}

static uint32_t get_time_ms(void)
{
    return 0;
}

static int g_lock_count = 0;
static uint32_t g_osal_time = 0;

static uint32_t get_time_mutable(void)
{
    return g_osal_time;
}

static void mock_pending_handler(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                                 uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_pending(out);
}

static void mock_mutex_lock(void *handle)
{
    int *val = (int *) handle;
    (*val)++;
    g_lock_count++;
}

static void mock_mutex_unlock(void *handle)
{
    int *val = (int *) handle;
    (*val)--;
}

static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[256];
static uint8_t tx_buf[256];
static int g_mutex_val = 0;

static int setup(void **state)
{
    (void) state;
    (void) state;
    memset(&ctx, 0, sizeof(ctx));
    memset(&cfg, 0, sizeof(cfg));
    g_mutex_val = 0;
    g_lock_count = 0;

    cfg.fn_tp_send = mock_can_send;
    cfg.get_time_ms = get_time_ms;
    cfg.mutex_handle = &g_mutex_val;
    cfg.fn_mutex_lock = mock_mutex_lock;
    cfg.fn_mutex_unlock = mock_mutex_unlock;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);

    uds_init(&ctx, &cfg);
    return 0;
}

static void test_osal_locking(void **state)
{
    (void) state;

    /* 1. Input SDU should lock and unlock */
    uint8_t req[] = {0x3E, 0x00};
    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_lock_count, 1);
    assert_int_equal(g_mutex_val, 0); /* Should be back to 0 */

    /* 2. Process should lock and unlock */
    uds_process(&ctx);
    assert_int_equal(g_lock_count, 2);
    assert_int_equal(g_mutex_val, 0);
}

/* The RCRRP-limit branch in uds_process() must release the mutex before
   returning. Otherwise the stack deadlocks the first time the 0x78 repeat
   limit is reached. */
static void test_osal_unlocks_on_rcrrp_limit(void **state)
{
    (void) state;

    static const uds_service_entry_t services[] = {
        {0x99, 1, UDS_SESSION_ALL, 0, mock_pending_handler, NULL, 0u}};

    memset(&ctx, 0, sizeof(ctx));
    memset(&cfg, 0, sizeof(cfg));
    g_mutex_val = 0;
    g_lock_count = 0;
    g_osal_time = 0;

    cfg.fn_tp_send = mock_can_send;
    cfg.get_time_ms = get_time_mutable;
    cfg.mutex_handle = &g_mutex_val;
    cfg.fn_mutex_lock = mock_mutex_lock;
    cfg.fn_mutex_unlock = mock_mutex_unlock;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 100;
    cfg.rcrrp_limit = 1;
    cfg.user_services = services;
    cfg.user_service_count = 1;

    uds_init(&ctx, &cfg);

    /* Start the async request: handler returns UDS_PENDING -> first 0x78. */
    uint8_t req[] = {0x99};
    uds_input_sdu(&ctx, req, 1);
    assert_true(ctx.server.p2_msg_pending);

    /* First P2* expiry: sends a repeated 0x78, rcrrp_count -> 1. */
    g_osal_time = 200;
    uds_process(&ctx);
    assert_int_equal(g_mutex_val, 0);

    /* Second P2* expiry: rcrrp_count (1) >= limit (1) -> abort branch.
       Must still unlock before returning. */
    g_osal_time = 400;
    uds_process(&ctx);
    assert_int_equal(g_mutex_val, 0); /* mutex must be released */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_osal_locking, setup),
        cmocka_unit_test(test_osal_unlocks_on_rcrrp_limit),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
