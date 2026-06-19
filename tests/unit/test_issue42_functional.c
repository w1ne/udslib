/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/* Issue #42 (part 2): functional addressing end to end. A functionally
 * addressed TesterPresent is serviced; a functional request for an unsupported
 * service is silently ignored (suppressed 0x11), while the same physical
 * request returns NRC 0x11. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

static uint8_t g_last_tx[8];
static uint8_t g_last_len;
static int g_can_calls;

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    g_can_calls++;
    if (len <= sizeof(g_last_tx)) {
        memcpy(g_last_tx, data, len);
        g_last_len = len;
    }
    return 0;
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx[64];
static uint8_t g_txbuf[64];
static uint32_t g_time;
static uint32_t time_ms(void)
{
    return g_time;
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_sdu[64];

/* Server response transport: send via ISO-TP on the physical tx_id. */
static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    return uds_isotp_send(&g_iso, data, len);
}

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.rx_buffer = g_rx;
    g_cfg.rx_buffer_size = sizeof(g_rx);
    g_cfg.tx_buffer = g_txbuf;
    g_cfg.tx_buffer_size = sizeof(g_txbuf);
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);

    g_time = 0;
    g_can_calls = 0;
    g_last_len = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E8, 0x7E0, g_sdu, sizeof(g_sdu));
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    return 0;
}

/* 14: functional TesterPresent is serviced (positive response emitted). */
static void test_functional_tester_present(void **state)
{
    (void) state;
    uint8_t tp[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7DF, tp, 8);
    assert_int_equal(g_can_calls, 1);     /* one response frame */
    assert_int_equal(g_last_tx[0], 0x02); /* SF, len 2 */
    assert_int_equal(g_last_tx[1], 0x7E); /* 0x3E | 0x40 */
}

/* 15: functional request for an unsupported SID -> silence; physical -> NRC 0x11. */
static void test_functional_unsupported_silent(void **state)
{
    (void) state;
    uint8_t req[8] = {0x01, 0xBF, 0, 0, 0, 0, 0, 0};      /* unknown SID 0xBF */
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7DF, req, 8); /* functional */
    assert_int_equal(g_can_calls, 0);                     /* suppressed: silence */

    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E0, req, 8); /* physical */
    assert_int_equal(g_can_calls, 1);
    assert_int_equal(g_last_tx[1], 0x7F); /* negative response */
    assert_int_equal(g_last_tx[2], 0xBF);
    assert_int_equal(g_last_tx[3], 0x11); /* serviceNotSupported */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_functional_tester_present, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_unsupported_silent, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
