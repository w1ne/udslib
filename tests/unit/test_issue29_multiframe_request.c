/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_issue29_multiframe_request.c
 * @brief Regression for issue #29: a multi-frame (FF+CF) request must be
 *        reassembled AND dispatched, producing a response on the bus.
 *
 * Unlike the other ISO-TP tests, this one does NOT --wrap uds_input_sdu: it
 * wires the real transport to the real core so the full path
 *   rx_callback -> reassembly -> uds_input_sdu -> dispatch -> response -> TX
 * is exercised. The frames replayed below are the exact bytes captured on the
 * reporter's PCAN bus (STM32F103 server):
 *
 *   tester -> ECU (0x111): 10 0B 27 01 5A 11 22 33   (FirstFrame, FF_DL=11)
 *   ECU -> tester (0x222): 30 08 00 ...              (FlowControl CTS)
 *   tester -> ECU (0x111): 21 44 55 66 77 88 ..      (ConsecutiveFrame SN=1)
 *   ECU -> tester (0x222): <SecurityAccess response> (this was MISSING)
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

#define ECU_RX_ID 0x111u /* tester -> ECU (requests)  */
#define ECU_TX_ID 0x222u /* ECU -> tester (responses) */

/* --- Captured bus frames (everything the ECU transmits) --- */
typedef struct
{
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} captured_frame_t;

static captured_frame_t g_tx_frames[16];
static int g_tx_count;

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_tx_sdu[1024];

static uds_ctx_t g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buf[1024];
static uint8_t g_tx_buf[1024];

static uint32_t g_time_ms;

static int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (g_tx_count < (int) (sizeof(g_tx_frames) / sizeof(g_tx_frames[0]))) {
        captured_frame_t *f = &g_tx_frames[g_tx_count++];
        f->id = id;
        f->len = (len > 8u) ? 8u : len;
        memcpy(f->data, data, f->len);
    }
    return 0;
}

static uint32_t get_time_ms(void)
{
    return g_time_ms;
}

/* Bridge the core's fn_tp_send contract to our ISO-TP instance. */
static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    return uds_isotp_send(&g_iso, data, len);
}

/* requestSeed handler: returns a fixed 4-byte seed (DE AD BE EF). */
static int security_seed(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len)
{
    (void) ctx;
    (void) level;
    (void) max_len;
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

static int setup(void **state)
{
    (void) state;
    g_tx_count = 0;
    g_time_ms = 0;
    memset(g_tx_frames, 0, sizeof(g_tx_frames));

    /* Classical CAN (the F103 has bxCAN, not CAN-FD). */
    uds_tp_isotp_init(&g_iso, can_send, ECU_TX_ID, ECU_RX_ID, g_iso_tx_sdu, sizeof(g_iso_tx_sdu));
    uds_tp_isotp_set_fd(&g_iso, false);

    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = get_time_ms;
    g_cfg.fn_tp_send = isotp_send_adapter;
    g_cfg.fn_security_seed = security_seed;
    g_cfg.rx_buffer = g_rx_buf;
    g_cfg.rx_buffer_size = sizeof(g_rx_buf);
    g_cfg.tx_buffer = g_tx_buf;
    g_cfg.tx_buffer_size = sizeof(g_tx_buf);
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 2000;

    uds_init(&g_ctx, &g_cfg);
    return 0;
}

/* Replay the captured FF + CF and require a response on the bus. */
static void test_multiframe_request_gets_response(void **state)
{
    (void) state;

    /* FirstFrame: 10 0B 27 01 5A 11 22 33 (FF_DL=11: SID 27, sub 01, +9 data) */
    uint8_t ff[8] = {0x10, 0x0B, 0x27, 0x01, 0x5A, 0x11, 0x22, 0x33};
    uds_isotp_rx_callback(&g_iso, &g_ctx, ECU_RX_ID, ff, 8);
    uds_process(&g_ctx);

    /* The ECU must answer the FF with a FlowControl CTS (0x30). */
    assert_int_equal(g_tx_count, 1);
    assert_int_equal(g_tx_frames[0].id, ECU_TX_ID);
    assert_int_equal(g_tx_frames[0].data[0] & 0xF0u, 0x30u);

    /* ConsecutiveFrame: 21 44 55 66 77 88 (SN=1, last 5 payload bytes) + pad. */
    uint8_t cf[8] = {0x21, 0x44, 0x55, 0x66, 0x77, 0x88, 0x55, 0x55};
    uds_isotp_rx_callback(&g_iso, &g_ctx, ECU_RX_ID, cf, 8);
    uds_process(&g_ctx);

    /* The reassembled request 27 01 5A 11 22 33 44 55 66 77 88 must be
       dispatched and produce a SecurityAccess response. Before the fix the
       ECU stayed silent here ("No response" on the tester). */
    assert_true(g_tx_count >= 2);

    const captured_frame_t *resp = &g_tx_frames[g_tx_count - 1];
    assert_int_equal(resp->id, ECU_TX_ID);

    /* Single-frame positive response: 06 67 01 DE AD BE EF */
    assert_int_equal(resp->data[0], 0x06); /* SF, length 6 */
    assert_int_equal(resp->data[1], 0x67); /* 0x27 + 0x40 */
    assert_int_equal(resp->data[2], 0x01); /* echoed subfunction */
    assert_int_equal(resp->data[3], 0xDE); /* seed... */
    assert_int_equal(resp->data[4], 0xAD);
    assert_int_equal(resp->data[5], 0xBE);
    assert_int_equal(resp->data[6], 0xEF);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_multiframe_request_gets_response, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
