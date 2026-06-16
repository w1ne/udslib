/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_fuzz_core.c
 * @brief Fuzz testing for the untrusted-input boundaries.
 *
 * Feeds random data into both the core SDU dispatcher (uds_input_sdu) and the
 * ISO-TP frame parser (uds_isotp_rx_callback), checking for crashes and for
 * reassembly that runs past the receive buffer. Uses permissive, non-cmocka
 * callbacks so any service/transport path may run without tripping strict mocks.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "uds/uds_isotp.h"

#define FUZZ_ITERATIONS 20000

/* --- Permissive callbacks (swallow everything) --- */

static uint32_t g_fz_time = 0;
static uint32_t fz_time(void)
{
    return g_fz_time;
}
static int fz_send(struct uds_ctx *ctx, const uint8_t *data, uint32_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return 0;
}
static int fz_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    (void) data;
    (void) len;
    return 0;
}

static bool valid_session(uint8_t s)
{
    return s == 0x01u || s == 0x02u || s == 0x03u;
}

/* Random SDUs into the core dispatcher must never crash, and the session must
   only ever hold a defined value. */
static void test_fuzz_sdu_layer(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    static uint8_t rxb[256];
    static uint8_t txb[256];

    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fz_time;
    cfg.fn_tp_send = fz_send;
    cfg.rx_buffer = rxb;
    cfg.rx_buffer_size = sizeof(rxb);
    cfg.tx_buffer = txb;
    cfg.tx_buffer_size = sizeof(txb);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    uds_init(&ctx, &cfg);

    srand(0xC0FFEEu); /* deterministic */
    uint8_t buf[256];

    for (int i = 0; i < FUZZ_ITERATIONS; i++) {
        uint32_t len = (uint16_t) (rand() % (int) sizeof(buf));
        for (uint16_t j = 0; j < len; j++) {
            buf[j] = (uint8_t) rand();
        }
        g_fz_time += (uint32_t) (rand() % 100);
        uds_input_sdu(&ctx, buf, len);
        uds_process(&ctx);
        assert_true(valid_session(ctx.active_session));
    }
}

static bool valid_isotp_state(uds_isotp_state_t s)
{
    return s == ISOTP_IDLE || s == ISOTP_RX_WAIT_CF || s == ISOTP_TX_WAIT_FC ||
           s == ISOTP_TX_SENDING_CF;
}

/* Random CAN frames into the ISO-TP parser must never crash, must keep the
   state machine in a defined state, and must never reassemble past the receive
   buffer. */
static void test_fuzz_isotp_framer(void **state)
{
    (void) state;

    uds_isotp_ctx_t iso;
    static uint8_t sdu[256];
    uds_tp_isotp_init(&iso, fz_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uds_ctx_t uds;
    uds_config_t cfg;
    static uint8_t rxb[128];
    memset(&uds, 0, sizeof(uds));
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fz_time;
    cfg.fn_tp_send = fz_send;
    cfg.rx_buffer = rxb;
    cfg.rx_buffer_size = sizeof(rxb);
    uds.config = &cfg;

    srand(0xBADC0DEu);
    uint8_t frame[64];

    for (int i = 0; i < FUZZ_ITERATIONS; i++) {
        uint8_t flen = (uint8_t) (1 + (rand() % 64));
        for (uint8_t j = 0; j < flen; j++) {
            frame[j] = (uint8_t) rand();
        }
        /* Feed the matching RX id so the SF/FF/CF/FC parser actually runs. */
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, frame, flen);
        if (rand() & 1) {
            uds_tp_isotp_process(&iso, (g_fz_time += 7u));
        }
        assert_true(iso.bytes_processed <= cfg.rx_buffer_size);
        assert_true(valid_isotp_state(iso.state));
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fuzz_sdu_layer),
        cmocka_unit_test(test_fuzz_isotp_framer),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
