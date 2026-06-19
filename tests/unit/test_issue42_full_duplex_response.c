/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/* Issue #42 regression: in full-duplex, an inbound frame arriving while the
 * server is streaming a multi-frame response must NOT abort that response. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

/* Captured TX frames (concatenated CF payloads reassembled here). */
static uint8_t g_reassembled[64];
static uint16_t g_reassembled_len;
static uint16_t g_tx_msg_len; /* total SDU length from FF header */
static int g_cf_count;

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    uint8_t pci = data[0] & 0xF0;
    if (pci == 0x10) { /* FF: 2-byte header, rest is payload */
        /* Decode FF_DL from bits [0..11] of bytes 0..1. */
        g_tx_msg_len = (uint16_t) (((uint16_t) (data[0] & 0x0Fu) << 8u) | (uint16_t) data[1]);
        uint8_t in_ff = (uint8_t) (len - 2u);
        memcpy(&g_reassembled[g_reassembled_len], &data[2], (size_t) in_ff);
        g_reassembled_len += (uint16_t) in_ff;
    }
    else if (pci == 0x20) { /* CF: 1-byte header */
        g_cf_count++;
        /* Clip to remaining expected bytes to ignore CAN padding zeros. */
        uint16_t remaining =
            (g_tx_msg_len > g_reassembled_len) ? (uint16_t) (g_tx_msg_len - g_reassembled_len) : 0u;
        uint8_t avail = (uint8_t) (len - 1u);
        uint8_t to_copy = (remaining < avail) ? (uint8_t) remaining : avail;
        memcpy(&g_reassembled[g_reassembled_len], &data[1], (size_t) to_copy);
        g_reassembled_len += (uint16_t) to_copy;
    }
    /* FC (0x30) from a transmitter context never occurs here. */
    return 0;
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buffer[256];
static uint8_t g_tx_buffer[256];
static uint32_t g_time;
static uint32_t time_ms(void)
{
    return g_time;
}
static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[256];

static int g_tp_send_calls;
static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_tp_send_calls++; /* TesterPresent positive response path */
    /*
     * Realistic re-entrant path: in a real deployment the application wires
     * fn_tp_send to uds_isotp_send on the same ISO-TP context.  The positive
     * response to TesterPresent is 2 bytes (0x7E 0x00), which fits in a Single
     * Frame.  uds_send_sf emits the SF without touching tx_state / tx_msg_len /
     * tx_bytes_processed / tx_sdu_buf, so the in-flight multi-frame TX state is
     * completely undisturbed.  This call exercises that guarantee: if the SF
     * send were to clobber the MF TX state the reassembly assertions below
     * would catch it, proving the headline #42 claim on a realistic code path.
     */
    return uds_isotp_send(&g_iso, data, len);
}

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.rx_buffer = g_rx_buffer;
    g_cfg.rx_buffer_size = sizeof(g_rx_buffer);
    g_cfg.tx_buffer = g_tx_buffer;
    g_cfg.tx_buffer_size = sizeof(g_tx_buffer);
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);

    g_time = 0;
    g_reassembled_len = 0;
    g_tx_msg_len = 0;
    g_cf_count = 0;
    g_tp_send_calls = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    return 0;
}

static void test_inbound_sf_does_not_abort_response(void **state)
{
    (void) state;

    /* 1. Server starts a 30-byte multi-frame response. */
    uint8_t resp[30];
    for (int i = 0; i < 30; i++) resp[i] = (uint8_t) i;
    assert_int_equal(uds_isotp_send(&g_iso, resp, 30), 0); /* emits FF (6 bytes) */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* 2. Inbound TesterPresent SF arrives mid-stream (functionally irrelevant). */
    uint8_t tp[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, tp, 8);
    /* Core serviced it — exactly one positive response, no double-dispatch. */
    assert_int_equal(g_tp_send_calls, 1);
    /* TX response untouched. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* 3. Tester (the receiver of our response) grants flow control; server streams remaining 24
     * bytes. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    for (int i = 0; i < 4; i++) {
        g_time += 1;
        uds_tp_isotp_process(&g_iso, g_time);
    }

    /* 4. The full 30-byte response was transmitted intact and in order. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
    assert_int_equal(g_reassembled_len, 30);
    assert_memory_equal(g_reassembled, resp, 30);
    assert_int_equal(g_cf_count, 4); /* 24 bytes / 7 per CF = 4 frames */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_inbound_sf_does_not_abort_response, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
