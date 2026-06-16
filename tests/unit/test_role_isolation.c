/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_role_isolation.c
 * @brief Server (async) and client (awaiting-response) state must be independent.
 *
 * The context used to track both with a single pending_sid. A server that
 * finished an asynchronous request left that field set, so a later request
 * whose SID equalled (previous SID | 0x40) was mistaken for a client response
 * and silently swallowed instead of being dispatched.
 */

#include "test_helpers.h"

static int async_handler(struct uds_ctx *ctx, const uint8_t *data, uint32_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return UDS_PENDING;
}

static void make_server(uds_ctx_t *ctx, uds_config_t *cfg, const uds_service_entry_t *svcs,
                        uint16_t n)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_rx_buf;
    cfg->rx_buffer_size = sizeof(g_rx_buf);
    cfg->tx_buffer = g_tx_buf;
    cfg->tx_buffer_size = sizeof(g_tx_buf);
    cfg->p2_ms = 50;
    cfg->p2_star_ms = 5000;
    cfg->user_services = svcs;
    cfg->user_service_count = n;
    uds_init(ctx, cfg);
}

static void test_server_async_does_not_swallow_later_request(void **state)
{
    (void) state;
    static const uds_service_entry_t services[] = {
        {0x31, 2, UDS_SESSION_ALL, 0, async_handler, NULL}};
    uds_ctx_t ctx;
    uds_config_t cfg;
    make_server(&ctx, &cfg, services, 1);

    /* A. Drive a server-side async request: 0x31 -> NRC 0x78 (pending). */
    uint8_t req31[] = {0x31, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 31 78 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req31, sizeof(req31));
    assert_true(ctx.p2_msg_pending);

    /* B. Application finishes the routine with a positive response (0x71). */
    ctx.config->tx_buffer[0] = 0x71;
    ctx.config->tx_buffer[1] = 0x01;
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_send_response(&ctx, 2);
    assert_false(ctx.p2_msg_pending);

    /* C. A brand-new request whose SID is (0x31 | 0x40) == 0x71 must be handled
          as a server request -- no such service -> NRC 0x11 -- not consumed as
          a stale "client response". */
    uint8_t req71[] = {0x71, 0x00};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 71 11 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req71, sizeof(req71));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x71);
    assert_int_equal(g_tx_buf[2], 0x11);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_server_async_does_not_swallow_later_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
