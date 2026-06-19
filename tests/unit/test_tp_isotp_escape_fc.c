/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_isotp_escape_fc.c
 * @brief ISO 15765-2 escape-FirstFrame (FF_DL > 4095) and FlowControl
 *        WAIT/OVFLW handling. Regression coverage for issue #27.
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

/* Mock CAN Send */
static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int) mock();
}

/* Mock Input SDU (captures reassembled RX payloads) */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected_ptr(data);
    check_expected(len);
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[8192];

/* Shared RX context plumbing */
static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buffer[8192];

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    uds_tp_isotp_set_fd(&g_iso, true);

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.rx_buffer = g_rx_buffer;
    g_cfg.rx_buffer_size = sizeof(g_rx_buffer);
    g_ctx.config = &g_cfg;
    return 0;
}

/* --- #4: escape FirstFrame TX (FF_DL > 4095) --- */
static void test_escape_ff_tx(void **state)
{
    (void) state;
    const uint16_t LEN = 5000; /* 0x1388, requires escape sequence */
    static uint8_t data[5000];
    for (int i = 0; i < (int) LEN; i++) data[i] = (uint8_t) (i & 0xFF);

    /* Escape FF: [10][00][00 00 13 88] + 58 data bytes -> DLC 64 */
    uint8_t expected[64] = {0};
    expected[0] = 0x10;
    expected[1] = 0x00;
    expected[2] = 0x00;
    expected[3] = 0x00;
    expected[4] = 0x13;
    expected[5] = 0x88;
    memcpy(&expected[6], data, 58);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_memory(mock_can_send, data, expected, 64);
    will_return(mock_can_send, 0);

    int rc = uds_isotp_send(&g_iso, data, LEN);
    assert_int_equal(rc, 0);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(g_iso.tx_msg_len, LEN);
    assert_int_equal(g_iso.tx_bytes_processed, 58);
}

/* --- #3: escape FirstFrame RX (FF_DL > 4095) full reassembly --- */
static void test_escape_ff_rx_reassembly(void **state)
{
    (void) state;
    const uint16_t LEN = 4096; /* smallest escape value */
    static uint8_t payload[4096];
    for (int i = 0; i < (int) LEN; i++) payload[i] = (uint8_t) ((i * 7) & 0xFF);

    /* Escape FF carries 58 bytes (64 - 6 header) */
    uint8_t ff[64] = {0};
    ff[0] = 0x10;
    ff[1] = 0x00;
    ff[2] = 0x00;
    ff[3] = 0x00;
    ff[4] = 0x10;
    ff[5] = 0x00; /* 0x1000 = 4096 */
    memcpy(&ff[6], payload, 58);

    /* FF must trigger a FlowControl CTS (classic 8-byte FC) */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);

    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 64);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);
    assert_int_equal(g_iso.rx_msg_len, LEN);

    /* Drive ConsecutiveFrames (FD CF carries 63 bytes) until complete */
    uint16_t sent = 58;
    uint8_t sn = 1;
    while (sent < LEN) {
        uint8_t cf[64] = {0};
        cf[0] = (uint8_t) (0x20 | (sn & 0x0F));
        uint16_t remaining = LEN - sent;
        uint8_t chunk = (remaining > 63) ? 63 : (uint8_t) remaining;
        memcpy(&cf[1], &payload[sent], chunk);

        if (sent + chunk >= LEN) {
            /* Final CF delivers the reassembled SDU */
            expect_memory(__wrap_uds_input_sdu, data, payload, LEN);
            expect_value(__wrap_uds_input_sdu, len, LEN);
        }
        uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf, (uint8_t) (1 + chunk));
        sent += chunk;
        sn = (sn + 1) & 0x0F;
    }
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

/* --- #3: oversize FF (FF_DL > rx buffer) must answer FC.OVFLW --- */
static void test_rx_ff_overflow(void **state)
{
    (void) state;
    g_cfg.rx_buffer_size = 64; /* tiny buffer */

    /* FD FF claiming 100 bytes */
    uint8_t ff[64] = {0};
    ff[0] = 0x10;
    ff[1] = 100;
    memset(&ff[2], 0xAB, 62);

    uint8_t expected_fc[8] = {0};
    expected_fc[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_OVA); /* 0x32 */

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_fc, 8);
    will_return(mock_can_send, 0);

    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 64);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

/* Helper: start a multi-frame TX and consume the FF */
static void start_mf_tx(uint16_t len)
{
    static uint8_t data[200];
    for (int i = 0; i < (int) len && i < 200; i++) data[i] = (uint8_t) i;

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);

    uds_isotp_send(&g_iso, data, len);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* --- #5: FC.WAIT keeps the sender waiting, CTS later resumes --- */
static void test_fc_wait_then_cts(void **state)
{
    (void) state;
    start_mf_tx(100);

    /* FC.WAIT: stay in WAIT_FC, send nothing */
    uint8_t fc_wait[8] = {0x31, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_wait, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* process() must NOT emit a CF while waiting (no mock expectation set) */
    uds_tp_isotp_process(&g_iso, 1);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* CTS now arrives -> resume sending */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_cts, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_SENDING_CF);

    expect_value(mock_can_send, id, 0x7E0);
    expect_any(mock_can_send, len);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(&g_iso, 10);
}

/* --- #5: FC.OVFLW aborts the transmission --- */
static void test_fc_overflow_aborts(void **state)
{
    (void) state;
    start_mf_tx(100);

    uint8_t fc_ovflw[8] = {0x32, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_ovflw, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);

    /* process() must be a no-op now (no CF) */
    uds_tp_isotp_process(&g_iso, 10);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- #5: reserved/invalid FlowStatus aborts (N_INVALID_FS) --- */
static void test_fc_invalid_fs_aborts(void **state)
{
    (void) state;
    start_mf_tx(100);

    uint8_t fc_bad[8] = {0x33, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_bad, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);

    uds_tp_isotp_process(&g_iso, 10);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- #1: classic CAN Single Frame RX regression --- */
static void test_classic_sf_rx(void **state)
{
    (void) state;
    uds_tp_isotp_set_fd(&g_iso, false);

    uint8_t sf[8] = {0x03, 0xAA, 0xBB, 0xCC, 0, 0, 0, 0};
    uint8_t expected[3] = {0xAA, 0xBB, 0xCC};

    expect_memory(__wrap_uds_input_sdu, data, expected, 3);
    expect_value(__wrap_uds_input_sdu, len, 3);

    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, sf, 8);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_escape_ff_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_escape_ff_rx_reassembly, setup, NULL),
        cmocka_unit_test_setup_teardown(test_rx_ff_overflow, setup, NULL),
        cmocka_unit_test_setup_teardown(test_fc_wait_then_cts, setup, NULL),
        cmocka_unit_test_setup_teardown(test_fc_overflow_aborts, setup, NULL),
        cmocka_unit_test_setup_teardown(test_fc_invalid_fs_aborts, setup, NULL),
        cmocka_unit_test_setup_teardown(test_classic_sf_rx, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
