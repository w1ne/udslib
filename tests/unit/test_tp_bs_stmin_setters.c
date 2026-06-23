/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_bs_stmin_setters.c
 * @brief Regression for §5c: uds_tp_isotp_set_block_size / uds_tp_isotp_set_st_min.
 *
 * Verifies that the FC frame emitted by the receiver on a First Frame
 * advertises the BS and STmin values configured via the new setters.
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

/* Capture the last CAN frame emitted by can_send. */
static uint8_t g_last_frame[8];
static int g_call_count;

static int capturing_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    g_call_count++;
    memset(g_last_frame, 0, sizeof(g_last_frame));
    if (len > sizeof(g_last_frame)) {
        len = (uint8_t) sizeof(g_last_frame);
    }
    memcpy(g_last_frame, data, len);
    return 0;
}

/* Build a minimal uds_ctx with a real rx_buffer so uds_rx_ff doesn't
 * overflow-abort and a no-op get_time_ms. */
static uint32_t zero_time(void)
{
    return 0u;
}

static void make_uds(uds_ctx_t *uds, uds_config_t *cfg, uint8_t *rxb, uint16_t rxn)
{
    memset(uds, 0, sizeof(*uds));
    memset(cfg, 0, sizeof(*cfg));
    cfg->get_time_ms = zero_time;
    cfg->rx_buffer = rxb;
    cfg->rx_buffer_size = rxn;
    uds->config = cfg;
}

/* §5c: after set_block_size(4) + set_st_min(0x05), the FC CTS frame emitted
 * on receiving a First Frame must advertise BS=4 and STmin=0x05. */
static void test_fc_advertises_configured_bs_and_stmin(void **state)
{
    (void) state;

    uds_isotp_ctx_t iso;
    uint8_t tx_sdu[256];
    uds_tp_isotp_init(&iso, capturing_can_send, 0x7E0, 0x7E8, tx_sdu, sizeof(tx_sdu));

    /* Defaults: BS=8, STmin=0 — verify baseline before changing. */
    assert_int_equal(iso.block_size, 8);
    assert_int_equal(iso.st_min, 0);

    /* Apply new values via setters. */
    uds_tp_isotp_set_block_size(&iso, 4);
    uds_tp_isotp_set_st_min(&iso, 0x05);

    assert_int_equal(iso.block_size, 4);
    assert_int_equal(iso.st_min, 0x05);

    /* Feed a multi-frame First Frame (20-byte SDU: 0x10 0x14 ...). */
    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[256];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    g_call_count = 0;
    memset(g_last_frame, 0, sizeof(g_last_frame));

    /* FF: PCI=0x10 | hi(20)=0x00 -> 0x10, len_lo=0x14, then 6 bytes of data. */
    uint8_t ff[8] = {0x10, 0x14, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, sizeof(ff));

    /* The engine must have emitted exactly one FC frame. */
    assert_int_equal(g_call_count, 1);

    /* FC byte 0: PCI(0x30) | FS(CTS=0x00) = 0x30. */
    assert_int_equal(g_last_frame[0], 0x30u);
    /* FC byte 1: Block Size as advertised. */
    assert_int_equal(g_last_frame[1], 4u);
    /* FC byte 2: STmin as advertised. */
    assert_int_equal(g_last_frame[2], 0x05u);
}

/* NULL-pointer safety: neither setter must crash on NULL. */
static void test_setters_null_guard(void **state)
{
    (void) state;
    uds_tp_isotp_set_block_size(NULL, 4);
    uds_tp_isotp_set_st_min(NULL, 0x05);
    /* Pass: no crash, no assertion. */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fc_advertises_configured_bs_and_stmin),
        cmocka_unit_test(test_setters_null_guard),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
