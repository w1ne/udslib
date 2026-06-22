/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_isotp_net_hardening.c
 * @brief ISO-TP transport edge-case regression tests: SN wrap, FF-abort in
 *        half-duplex, N_Bs timeout after FC.WAIT, RX buffer boundary (exact
 *        fit and one-over), STmin=0x7F, and N_Cr timer refresh.
 *
 * These tests lock the current correct behaviour so future changes cannot
 * silently regress it. No production code is modified.
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

/* ------------------------------------------------------------------ */
/*  Shared mock infrastructure                                         */
/* ------------------------------------------------------------------ */

static uint32_t g_time = 0;
static uint32_t time_src(void)
{
    return g_time;
}

/* Capture array for outgoing frames. */
#define MAX_CAPTURES 64
static uint8_t g_captured[MAX_CAPTURES][ISOTP_MAX_DL_CAN];
static int g_capture_count = 0;

static int capturing_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    if (g_capture_count < MAX_CAPTURES) {
        uint8_t copy_len = len < ISOTP_MAX_DL_CAN ? len : ISOTP_MAX_DL_CAN;
        memcpy(g_captured[g_capture_count], data, copy_len);
        g_capture_count++;
    }
    return 0;
}

static void reset_captures(void)
{
    memset(g_captured, 0, sizeof(g_captured));
    g_capture_count = 0;
    g_time = 0;
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

/* ------------------------------------------------------------------ */
/*  Test 1: TX sequence-number wrap (SN 1..15 then 0 then 1...)        */
/* ------------------------------------------------------------------ */

/*
 * Classic CAN: FF carries 6 data bytes; each CF carries 7.
 * To get SN 1..15 then wrap to 0 and continue to at least 1 again:
 *   need >= 17 CFs => 6 + 17*7 = 125 bytes minimum.
 * Use 135 bytes: FF=6, 19 CFs of 7 each (6+19*7=139 but last CF gets 1 byte).
 * Actually: 6 + 18*7 = 132; 19th CF gets 135-6-18*7 = 135-132 = 3 bytes.
 * SN sequence: 1,2,...,15,0,1,2,3 (19 CFs total, wraps once at index 16->0).
 */
static void test_tx_sn_wrap(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[200];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    const uint16_t PAYLOAD_LEN = 135;
    uint8_t data[135];
    for (int i = 0; i < PAYLOAD_LEN; i++) data[i] = (uint8_t) i;

    /* Send: triggers FF */
    int rc = uds_isotp_send(&iso, data, PAYLOAD_LEN);
    assert_int_equal(rc, 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(g_capture_count, 1); /* FF only */

    /* FF: PCI byte upper nibble = 1, SN starts at 1 (set in uds_send_mf) */
    assert_int_equal(g_captured[0][0] & 0xF0, ISOTP_PCI_FF);
    assert_int_equal(iso.tx_sn, 1u);

    /* FC CTS: BS=0 (unlimited), STmin=0 */
    uint8_t fc[8] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, 8);
    assert_int_equal(iso.tx_state, ISOTP_TX_SENDING_CF);

    /* Drive all CFs out. With STmin=0 each process() tick at a different
     * time_ms sends one CF. */
    int cf_index = 0;
    while (iso.tx_state == ISOTP_TX_SENDING_CF) {
        g_time += 10;
        uds_tp_isotp_process(&iso, g_time);
        cf_index++;
        if (cf_index > 30) break; /* guard against infinite loop */
    }
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);

    /* total captured = 1 FF + 19 CFs = 20 */
    assert_int_equal(g_capture_count, 20);

    /* Verify SN sequence in CFs (captures 1..19) */
    uint8_t expected_sn[19] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                0, 1, 2, 3};
    for (int i = 0; i < 19; i++) {
        uint8_t pci = g_captured[1 + i][0];
        assert_int_equal(pci & 0xF0, ISOTP_PCI_CF);
        assert_int_equal(pci & 0x0F, expected_sn[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  Test 2: Half-duplex FF-abort: incoming FF kills in-progress TX     */
/* ------------------------------------------------------------------ */

static void test_half_duplex_ff_aborts_tx(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[200];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));
    /* default mode is ISOTP_HALF_DUPLEX */

    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[64];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    /* Start a multi-frame TX (20 bytes) */
    uint8_t out[20];
    memset(out, 0xAA, sizeof(out));
    assert_int_equal(uds_isotp_send(&iso, out, sizeof(out)), 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Inbound FF (16-byte SDU): in half-duplex this must abort the TX */
    g_time = 500;
    uint8_t ff[8] = {0x10, 0x10, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);

    /* TX must be aborted */
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    assert_int_equal(iso.timer_n_bs, 0u);
    /* No FC.CTS was received here, so this was already 0; the assert locks the
     * defensive clear in the FF-abort path rather than a 1->0 transition. */
    assert_int_equal(iso.first_cf_after_fc, 0u);

    /* RX must be active (waiting for CFs) */
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

    /* A CTS that arrives after abort must have NO effect on TX (it is IDLE) */
    int prev_count = g_capture_count;
    uint8_t fc[8] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, fc, 8);
    uds_tp_isotp_process(&iso, 600);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    /* no extra CAN frames beyond the FC.CTS that uds_rx_ff emitted */
    assert_int_equal(g_capture_count, prev_count);
}

/* ------------------------------------------------------------------ */
/*  Test 3: N_Bs timeout fires after FC.WAIT re-arms it               */
/* ------------------------------------------------------------------ */

/*
 * Sequence:
 *  1. send() -> FF -> WAIT_FC
 *  2. FC.WAIT received -> stays WAIT_FC, N_Bs re-armed (timer_n_bs reset to 0)
 *  3. first process() tick arms timer_n_bs again
 *  4. after N_Bs ms from that arm time: aborts to IDLE
 */
static void test_n_bs_timeout_after_fc_wait(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xBB, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Arm N_Bs at t=10 */
    uds_tp_isotp_process(&iso, 10);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(iso.timer_n_bs, 10u);

    /* FC.WAIT: re-arm (timer_n_bs reset to 0, will re-arm on next process()) */
    uint8_t fc_wait[8] = {0x31, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc_wait, 8);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(iso.timer_n_bs, 0u); /* re-armed indicator: cleared */

    /* Re-arm: next process() tick (t=500) sets timer_n_bs=500 */
    uds_tp_isotp_process(&iso, 500);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(iso.timer_n_bs, 500u);

    /* Still within N_Bs window */
    uds_tp_isotp_process(&iso, 500 + ISOTP_N_BS_DEFAULT_MS - 1u);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Past N_Bs: abort */
    uds_tp_isotp_process(&iso, 500u + ISOTP_N_BS_DEFAULT_MS + 1u);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    assert_int_equal(iso.timer_n_bs, 0u);
}

/* ------------------------------------------------------------------ */
/*  Test 4: FF_DL exactly equals RX buffer size — must be accepted     */
/* ------------------------------------------------------------------ */

static void test_rx_ff_dl_exact_buffer_fit(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    const uint16_t BUF_SIZE = 20;
    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[BUF_SIZE];
    make_uds(&uds, &cfg, rxb, BUF_SIZE);

    /* FF claiming exactly BUF_SIZE bytes */
    uint8_t ff[8];
    memset(ff, 0xCC, sizeof(ff));
    ff[0] = (uint8_t) (ISOTP_PCI_FF | ((BUF_SIZE >> 8) & 0x0F));
    ff[1] = (uint8_t) (BUF_SIZE & 0xFF);
    uint8_t payload[BUF_SIZE];
    for (int i = 0; i < BUF_SIZE; i++) payload[i] = (uint8_t) i;
    memcpy(&ff[2], payload, 6); /* FF carries first 6 bytes */

    g_time = 100;
    /* Must trigger FC.CTS (one outgoing frame) */
    int before = g_capture_count;
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);

    /* Accepted: RX is now waiting for CFs */
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);
    assert_int_equal(iso.rx_msg_len, BUF_SIZE);
    /* FC.CTS should have been emitted */
    assert_int_equal(g_capture_count, before + 1);
    assert_int_equal(g_captured[before][0] & 0xF0, ISOTP_PCI_FC);
    assert_int_equal(g_captured[before][0] & 0x0F, ISOTP_FC_CTS);

    /* Drive remaining 14 bytes in via CFs */
    uint8_t cf1[8];
    memset(cf1, 0xCC, sizeof(cf1));
    cf1[0] = 0x21;
    memcpy(&cf1[1], &payload[6], 7);
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf1, 8);
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF); /* 13 bytes left */

    uint8_t cf2[8];
    memset(cf2, 0xCC, sizeof(cf2));
    cf2[0] = 0x22;
    memcpy(&cf2[1], &payload[13], 7); /* last 7 bytes */
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf2, 8);
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE); /* complete */
}

/* ------------------------------------------------------------------ */
/*  Test 5: FF_DL one larger than buffer — must send FC.OVFLW         */
/* ------------------------------------------------------------------ */

static void test_rx_ff_dl_one_over_buffer(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    const uint16_t BUF_SIZE = 20;
    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[BUF_SIZE];
    make_uds(&uds, &cfg, rxb, BUF_SIZE);

    const uint16_t CLAIM = BUF_SIZE + 1; /* one over */
    uint8_t ff[8];
    memset(ff, 0xCC, sizeof(ff));
    ff[0] = (uint8_t) (ISOTP_PCI_FF | ((CLAIM >> 8) & 0x0F));
    ff[1] = (uint8_t) (CLAIM & 0xFF);

    int before = g_capture_count;
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);

    /* Must have sent exactly one frame: FC.OVFLW */
    assert_int_equal(g_capture_count, before + 1);
    uint8_t fc_byte = g_captured[before][0];
    assert_int_equal(fc_byte & 0xF0, ISOTP_PCI_FC);
    assert_int_equal(fc_byte & 0x0F, ISOTP_FC_OVA);

    /* RX must stay IDLE */
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE);
}

/* ------------------------------------------------------------------ */
/*  Test 6a: STmin = 0x7F (127 ms) is honoured between CFs            */
/* ------------------------------------------------------------------ */

static void test_tx_stmin_0x7f(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);

    /* FC CTS: BS=0, STmin=0x7F (127 ms) */
    uint8_t fc[8] = {0x30, 0x00, 0x7F, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, 8);
    assert_int_equal(iso.tx_state, ISOTP_TX_SENDING_CF);
    assert_int_equal(iso.tx_st_min, 0x7Fu);

    int before = g_capture_count;

    /* First CF must go immediately (first_cf_after_fc exemption) */
    uds_tp_isotp_process(&iso, 10);
    assert_int_equal(g_capture_count, before + 1); /* CF1 sent */

    /* 126 ms elapsed since CF1: must NOT send CF2 yet (< 127 ms) */
    uds_tp_isotp_process(&iso, 10 + 126);
    assert_int_equal(g_capture_count, before + 1); /* still CF1 only */

    /* 128 ms elapsed: must send CF2 */
    uds_tp_isotp_process(&iso, 10 + 128);
    assert_int_equal(g_capture_count, before + 2); /* CF2 sent */

    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
}

/* ------------------------------------------------------------------ */
/*  Test 6b: STmin = 0xF1..0xF9 (microsecond band) decodes to 1 ms   */
/* ------------------------------------------------------------------ */

static void test_tx_stmin_microsecond_band_0xf1(void **state)
{
    (void) state;
    reset_captures();

    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xBB, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);

    /* FC CTS: BS=0, STmin=0xF1 (100 µs, treated as 1 ms at ms resolution) */
    uint8_t fc[8] = {0x30, 0x00, 0xF1, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, 8);
    assert_int_equal(iso.tx_state, ISOTP_TX_SENDING_CF);

    int before = g_capture_count;

    /* CF1: immediate (first_cf_after_fc) */
    uds_tp_isotp_process(&iso, 100);
    assert_int_equal(g_capture_count, before + 1);

    /* 0 ms elapsed from CF1 at t=100: must NOT send (elapsed 0 < required 1) */
    uds_tp_isotp_process(&iso, 100);
    assert_int_equal(g_capture_count, before + 1);

    /* 1 ms elapsed: must send CF2 */
    uds_tp_isotp_process(&iso, 101);
    assert_int_equal(g_capture_count, before + 2);

    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
}

/* ------------------------------------------------------------------ */
/*  Test 7: N_Cr timer is refreshed by each received CF               */
/* ------------------------------------------------------------------ */

/*
 * Use n_cr_ms = 100 ms. Send CFs spaced 90 ms apart (< N_Cr each).
 * Verify each CF resets the timer and the transfer completes.
 * Then do the same starting transfer but delay the SECOND CF by > N_Cr;
 * verify the RX aborts.
 */
static void test_n_cr_refresh_on_cf(void **state)
{
    (void) state;
    reset_captures(); /* Part A asserts absolute timestamps; don't inherit prior g_time */

    /* --- Part A: CFs within N_Cr each → full completion --- */
    {
        uds_isotp_ctx_t iso;
        uint8_t sdu[64];
        uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));
        iso.n_cr_ms = 100u;

        uds_ctx_t uds;
        uds_config_t cfg;
        uint8_t rxb[64];
        make_uds(&uds, &cfg, rxb, sizeof(rxb));

        /* FF for 22-byte SDU (FF=6 bytes, then 3 CFs of 7,7,2 bytes) */
        g_time = 1000;
        uint8_t payload[22];
        for (int i = 0; i < 22; i++) payload[i] = (uint8_t) (0x10 + i);

        uint8_t ff[8];
        memset(ff, 0xCC, sizeof(ff));
        ff[0] = 0x10;
        ff[1] = 22;
        memcpy(&ff[2], payload, 6);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);
        assert_int_equal(iso.timer_n_cr, 1000u);

        /* CF1 at t=1090 (90 ms < N_Cr=100): resets timer */
        g_time = 1090;
        uint8_t cf1[8] = {0x21, 0, 0, 0, 0, 0, 0, 0};
        memcpy(&cf1[1], &payload[6], 7);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf1, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);
        assert_int_equal(iso.timer_n_cr, 1090u); /* refreshed */

        /* Check N_Cr not yet expired: tick at 1090+99=1189 must keep WAIT_CF */
        uds_tp_isotp_process(&iso, 1189);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

        /* CF2 at t=1170 (80 ms from CF1's timer base 1090): resets again */
        g_time = 1170;
        uint8_t cf2[8] = {0x22, 0, 0, 0, 0, 0, 0, 0};
        memcpy(&cf2[1], &payload[13], 7);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf2, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);
        assert_int_equal(iso.timer_n_cr, 1170u);

        /* CF3 (final) at t=1250 (80 ms from 1170): completes reassembly */
        g_time = 1250;
        uint8_t cf3[8] = {0x23, 0, 0, 0, 0, 0, 0, 0};
        memcpy(&cf3[1], &payload[20], 2);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf3, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_IDLE); /* done */
    }

    /* --- Part B: second CF delayed > N_Cr → RX aborts --- */
    {
        reset_captures();
        uds_isotp_ctx_t iso;
        uint8_t sdu[64];
        uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));
        iso.n_cr_ms = 100u;

        uds_ctx_t uds;
        uds_config_t cfg;
        uint8_t rxb[64];
        make_uds(&uds, &cfg, rxb, sizeof(rxb));

        uint8_t payload[22];
        for (int i = 0; i < 22; i++) payload[i] = (uint8_t) (0x20 + i);

        g_time = 2000;
        uint8_t ff[8];
        memset(ff, 0xCC, sizeof(ff));
        ff[0] = 0x10;
        ff[1] = 22;
        memcpy(&ff[2], payload, 6);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

        /* CF1 at t=2090 (within N_Cr): accepted, timer_n_cr = 2090 */
        g_time = 2090;
        uint8_t cf1[8] = {0x21, 0, 0, 0, 0, 0, 0, 0};
        memcpy(&cf1[1], &payload[6], 7);
        uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf1, 8);
        assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);
        assert_int_equal(iso.timer_n_cr, 2090u);

        /* No CF2 arrives. tick at 2090+101=2191: N_Cr expired → IDLE */
        uds_tp_isotp_process(&iso, 2191);
        assert_int_equal(iso.rx_state, ISOTP_RX_IDLE);
    }
}

/* ------------------------------------------------------------------ */
/*  Test runner                                                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_tx_sn_wrap),
        cmocka_unit_test(test_half_duplex_ff_aborts_tx),
        cmocka_unit_test(test_n_bs_timeout_after_fc_wait),
        cmocka_unit_test(test_rx_ff_dl_exact_buffer_fit),
        cmocka_unit_test(test_rx_ff_dl_one_over_buffer),
        cmocka_unit_test(test_tx_stmin_0x7f),
        cmocka_unit_test(test_tx_stmin_microsecond_band_0xf1),
        cmocka_unit_test(test_n_cr_refresh_on_cf),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
