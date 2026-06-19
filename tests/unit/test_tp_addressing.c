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
    (void) id;
    (void) data;
    (void) len;
    return 0;
}

/* Physical deliveries land here (transport calls uds_input_sdu). */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
}

/* Functional deliveries land here (transport calls uds_input_sdu_addr). */
void __wrap_uds_input_sdu_addr(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                               uds_addr_mode_t addr)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
    check_expected(addr);
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_sdu[256];

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_sdu, sizeof(g_sdu));
    return 0;
}

/* 1: frame on rx_id -> physical entry. */
static void test_physical_sf(void **state)
{
    (void) state;
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uint8_t expect[2] = {0x3E, 0x00};
    expect_value(__wrap_uds_input_sdu, len, 2);
    expect_memory(__wrap_uds_input_sdu, data, expect, 2);
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, sf, 8);
}

/* 2: frame on rx_id_func -> functional entry with UDS_ADDR_FUNCTIONAL. */
static void test_functional_sf(void **state)
{
    (void) state;
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uint8_t expect[2] = {0x3E, 0x00};
    expect_value(__wrap_uds_input_sdu_addr, len, 2);
    expect_memory(__wrap_uds_input_sdu_addr, data, expect, 2);
    expect_value(__wrap_uds_input_sdu_addr, addr, UDS_ADDR_FUNCTIONAL);
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, sf, 8);
}

/* 3: functional disabled by default (rx_id_func == 0): foreign ID ignored. */
static void test_functional_disabled(void **state)
{
    (void) state;
    /* no functional id set */
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, sf, 8); /* not rx_id, func disabled -> ignored */
    /* no expectation set on either wrap => any delivery fails the test */
}

/* 4: functionally addressed FF is ignored (SF-only). */
static void test_functional_ff_ignored(void **state)
{
    (void) state;
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    uint8_t ff[8] = {0x10, 0x14, 0, 0, 0, 0, 0, 0}; /* FF, 20 bytes */
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, ff, 8);
    /* no FC emitted (mock_can_send asserts nothing, but a real FC would be a bug:
       the functional path never calls uds_rx_ff), and no delivery occurs. */
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_physical_sf, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_sf, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_disabled, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_ff_ignored, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
