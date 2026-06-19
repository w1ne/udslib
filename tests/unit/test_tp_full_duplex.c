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
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int) mock();
}

/* RX completion interception (linker --wrap). */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buffer[4096];
static uint32_t g_time;
static uint32_t time_ms(void)
{
    return g_time;
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[1024];

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.rx_buffer = g_rx_buffer;
    g_cfg.rx_buffer_size = sizeof(g_rx_buffer);
    g_cfg.get_time_ms = time_ms;
    g_ctx.config = &g_cfg;
    g_time = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    return 0;
}

/* Start a 30-byte classic-CAN multi-frame TX; consume the emitted FF. */
static void start_tx_30(void)
{
    static uint8_t data[30];
    for (int i = 0; i < 30; i++) data[i] = (uint8_t) (0xA0 + i);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    assert_int_equal(uds_isotp_send(&g_iso, data, 30), 0);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* --- 1: half-duplex incoming SF aborts active TX (behavior lock) --- */
static void test_half_duplex_sf_aborts_tx(void **state)
{
    (void) state;
    /* default mode is half-duplex */
    start_tx_30();

    /* Inbound SF (3 data bytes) must be delivered AND must abort the TX. */
    uint8_t sf[8] = {0x03, 0x11, 0x22, 0x33, 0, 0, 0, 0};
    uint8_t expected_sf[3] = {0x11, 0x22, 0x33};
    expect_value(__wrap_uds_input_sdu, len, 3);
    expect_memory(__wrap_uds_input_sdu, data, expected_sf, 3);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, sf, 8);

    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);

    /* A subsequent CTS + process() must emit NO consecutive frame. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    uds_tp_isotp_process(&g_iso, 10); /* no mock expectation -> any send fails the test */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 2: full-duplex incoming SF does NOT abort active TX --- */
static void test_full_duplex_sf_keeps_tx(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Inbound SF delivered; TX must remain in WAIT_FC. */
    uint8_t sf[8] = {0x03, 0x44, 0x55, 0x66, 0, 0, 0, 0};
    uint8_t expected_sf[3] = {0x44, 0x55, 0x66};
    expect_value(__wrap_uds_input_sdu, len, 3);
    expect_memory(__wrap_uds_input_sdu, data, expected_sf, 3);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, sf, 8);

    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* CTS resumes the original response: remaining 24 bytes => CF1(7)+CF2(7)+CF3(7)+CF4(3). */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_SENDING_CF);

    for (int i = 0; i < 4; i++) {
        expect_value(mock_can_send, id, 0x7E0);
        expect_value(mock_can_send, len, 8);
        expect_any(mock_can_send, data);
        will_return(mock_can_send, 0);
        uds_tp_isotp_process(&g_iso, (uint32_t) (10 + i));
    }
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 3: full-duplex simultaneous segmented RX and TX both complete --- */
static void test_full_duplex_simultaneous_rx_tx(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Inbound FF for a 14-byte reception: FF carries 6 bytes, then 2 CFs. */
    static uint8_t in_payload[14];
    for (int i = 0; i < 14; i++) in_payload[i] = (uint8_t) (0x10 + i);

    uint8_t ff[8] = {0x10, 0x0E, 0, 0, 0, 0, 0, 0}; /* FF_DL=14 */
    memcpy(&ff[2], in_payload, 6);
    /* FF must produce our FC.CTS (advertised BS=8, STmin=0). */
    uint8_t expected_fc[8] = {0x30, 0x08, 0x00, 0, 0, 0, 0, 0};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_fc, 8);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);

    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX untouched */

    /* CF1 (7 bytes) */
    uint8_t cf1[8] = {0x21, 0, 0, 0, 0, 0, 0, 0};
    memcpy(&cf1[1], &in_payload[6], 7);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf1, 8);

    /* CF2 (final, 1 byte) completes reassembly -> delivered. */
    uint8_t cf2[8] = {0x22, 0, 0, 0, 0, 0, 0, 0};
    memcpy(&cf2[1], &in_payload[13], 1);
    expect_value(__wrap_uds_input_sdu, len, 14);
    expect_memory(__wrap_uds_input_sdu, data, in_payload, 14);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf2, 8);

    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX still pending its FC */

    /* Now drive the TX to completion. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    for (int i = 0; i < 4; i++) {
        expect_value(mock_can_send, id, 0x7E0);
        expect_value(mock_can_send, len, 8);
        expect_any(mock_can_send, data);
        will_return(mock_can_send, 0);
        uds_tp_isotp_process(&g_iso, (uint32_t) (20 + i));
    }
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 4: independent timers — N_Cr expiry resets RX, leaves TX --- */
static void test_full_duplex_independent_timers(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Start a reception so rx_state == WAIT_CF, timer_n_cr seeded at g_time. */
    static uint8_t in_payload[14];
    for (int i = 0; i < 14; i++) in_payload[i] = (uint8_t) i;
    uint8_t ff[8] = {0x10, 0x0E, 0, 0, 0, 0, 0, 0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    g_time = 100;
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Advance beyond N_Cr (default 1000ms). RX must reset; TX must remain WAIT_FC.
       (TX timer_n_bs arms here but N_Bs default is 1000ms; 100+1100 - arm(=1101) < 1000,
       so TX has NOT yet timed out at this tick.) */
    uds_tp_isotp_process(&g_iso, 1101); /* arms N_Bs at 1101; N_Cr: 1101-100 >= 1000 -> reset RX */
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* --- 5: FC ignored when only RX active; CF ignored when only TX active --- */
static void test_cross_direction_frames_ignored(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);

    /* Only a TX is active: an inbound CF must be ignored (no crash, TX intact). */
    start_tx_30();
    uint8_t cf[8] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Only an RX is active (fresh ctx): an inbound FC must be ignored. */
    setup(state);
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0, 0, 0, 0, 0, 0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF); /* unaffected */
}

/* --- 6: half-duplex send() aborts active RX (behavior lock) --- */
static void test_half_duplex_send_aborts_rx(void **state)
{
    (void) state;
    /* default half-duplex */
    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0, 0, 0, 0, 0, 0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Start a TX: RX must be abandoned. */
    start_tx_30();
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

/* --- 7: wrong-SN during full-duplex RX aborts only RX, TX continues --- */
static void test_full_duplex_wrong_sn_isolated(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0, 0, 0, 0, 0, 0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);

    /* CF with wrong SN (expected 1, send 5) aborts RX. */
    uint8_t bad_cf[8] = {0x25, 1, 2, 3, 4, 5, 6, 7};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, bad_cf, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX untouched */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_half_duplex_sf_aborts_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_sf_keeps_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_simultaneous_rx_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_independent_timers, setup, NULL),
        cmocka_unit_test_setup_teardown(test_cross_direction_frames_ignored, setup, NULL),
        cmocka_unit_test_setup_teardown(test_half_duplex_send_aborts_rx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_wrong_sn_isolated, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
