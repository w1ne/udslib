/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * Issue #67: the byte used to pad unused frame bytes must be configurable
 * (0x00 default, e.g. 0xAA / 0xCC for bus partners that require it). The pad
 * value applies to every transmitted frame: SF, FF, CF and FC.
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

void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[1024];

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    return 0;
}

static int teardown(void **state)
{
    (void) state;
    return 0;
}

/* Default pad byte is 0x00 (preserves prior behavior). */
static void test_default_pad_is_zero(void **state)
{
    (void) state;
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t expected[] = {0x03, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected, 8);
    will_return(mock_can_send, 0);

    assert_int_equal(uds_isotp_send(&g_iso, data, 3), 0);
}

/* Single Frame is padded with the configured value. */
static void test_sf_uses_configured_pad(void **state)
{
    (void) state;
    uds_tp_isotp_set_pad_byte(&g_iso, 0xCC);

    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t expected[] = {0x03, 0x01, 0x02, 0x03, 0xCC, 0xCC, 0xCC, 0xCC};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected, 8);
    will_return(mock_can_send, 0);

    assert_int_equal(uds_isotp_send(&g_iso, data, 3), 0);
}

/* A short final Consecutive Frame is padded with the configured value. */
static void test_cf_uses_configured_pad(void **state)
{
    (void) state;
    uds_tp_isotp_set_pad_byte(&g_iso, 0xAA);

    /* 10 bytes -> FF (6 data) + one CF (4 data, padded to 8). */
    uint8_t data[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    uint8_t expected_ff[] = {0x10, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);
    uds_isotp_send(&g_iso, data, 10);

    /* Receive FC (CTS, BS=0, STmin=0). */
    uint8_t fc_frame[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_frame, 8);

    /* CF: [21] [07 08 09 0A] padded with 0xAA. */
    uint8_t expected_cf[] = {0x21, 0x07, 0x08, 0x09, 0x0A, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(&g_iso, 0);
}

/* The Flow Control frame the stack sends is padded with the configured value. */
static void test_fc_uses_configured_pad(void **state)
{
    (void) state;
    uds_tp_isotp_set_pad_byte(&g_iso, 0xCC);

    struct uds_ctx ctx;
    uds_config_t config;
    uint8_t rx_buffer[64];
    memset(&ctx, 0, sizeof(ctx));
    memset(&config, 0, sizeof(config));
    config.rx_buffer = rx_buffer;
    config.rx_buffer_size = sizeof(rx_buffer);
    ctx.config = &config;

    /* Inject a First Frame (SDU len 10); the stack must answer with FC CTS. */
    uint8_t ff_frame[] = {0x10, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    /* FC CTS: [30] [BS=8 (default)] [STmin=0] then padding 0xCC. */
    uint8_t expected_fc[] = {0x30, 0x08, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_fc, 8);
    will_return(mock_can_send, 0);

    uds_isotp_rx_callback(&g_iso, &ctx, 0x7E8, ff_frame, 8);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_default_pad_is_zero, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sf_uses_configured_pad, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cf_uses_configured_pad, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fc_uses_configured_pad, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
