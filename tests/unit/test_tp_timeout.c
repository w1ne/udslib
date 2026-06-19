/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_timeout.c
 * @brief ISO-TP reception (N_Cr) and transmission (N_Bs) timeouts.
 *
 * A multi-frame transfer must not wedge the engine forever when the peer goes
 * silent: a missing consecutive frame (RX) or a missing flow-control frame (TX)
 * has to time out back to IDLE.
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

static uint32_t g_time = 0;
static uint32_t time_src(void)
{
    return g_time;
}

static int silent_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    (void) data;
    (void) len;
    return 0;
}

static void make_uds(uds_ctx_t *uds, uds_config_t *cfg, uint8_t *rxb, uint16_t rxn)
{
    memset(uds, 0, sizeof(*uds));
    memset(cfg, 0, sizeof(*cfg));
    cfg->get_time_ms = time_src;
    cfg->rx_buffer = rxb;
    cfg->rx_buffer_size = rxn;
    uds->config = cfg;
}

/* RX: First Frame received, then the peer never sends a consecutive frame. */
static void test_rx_times_out_when_no_cf(void **state)
{
    (void) state;

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, silent_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[64];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    g_time = 1000;
    uint8_t ff[8] = {0x10, 0x14, 1, 2, 3, 4, 5, 6}; /* SDU length 20 */
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Still within N_Cr: keep waiting. */
    uds_tp_isotp_process(&iso, 1500);
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Past N_Cr: abort back to IDLE. */
    uds_tp_isotp_process(&iso, 1000 + ISOTP_N_CR_DEFAULT_MS + 1u);
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE);
}

/* TX: First Frame sent, then the peer never sends flow control. */
static void test_tx_times_out_when_no_fc(void **state)
{
    (void) state;

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, silent_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Arm and stay within N_Bs. */
    uds_tp_isotp_process(&iso, 10);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Past N_Bs with no FC: abort. */
    uds_tp_isotp_process(&iso, 10 + ISOTP_N_BS_DEFAULT_MS + 1u);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rx_times_out_when_no_cf),
        cmocka_unit_test(test_tx_times_out_when_no_fc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
