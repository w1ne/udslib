/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "test_helpers.h"

static uds_ctx_t g_ctx;
static uds_config_t g_cfg;
static bool g_safe_state = true;

/* Mock Safety Gate */
static bool mock_is_safe(struct uds_ctx *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    /* Reject SID 0x11 (Reset) if g_safe_state is false */
    if (sid == 0x11 && !g_safe_state) {
        return false;
    }
    return true; /* Allow everything else */
}

/* Local time provider override */
static uint32_t my_get_time(void)
{
    return 0;
}

static int teardown(void **state)
{
    (void) state;
    return 0;
}

/* 1. Test Unsafe Condition (NRC 0x22) */
static void test_safety_check_fails(void **state)
{
    (void) state;
    g_safe_state = false;
    uint8_t req[] = {0x11, 0x01}; /* Reset */

    /* Expect NRC 7F 11 22 (ConditionsNotCorrect) */
    uint8_t expected[] = {0x7F, 0x11, 0x22};

    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

/* 2. Test Safe Condition (Success) */
static void test_safety_check_passes(void **state)
{
    (void) state;
    g_safe_state = true;
    uint8_t req[] = {0x11, 0x01};

    /* With setup_full, mock_service_handler_full writes (SID|0x40) = 0x51 and
     * reports len=1. Expect positive response of 1 byte. */
    uint8_t expected[] = {0x51};
    expect_memory(mock_tp_send, data, expected, 1);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

/* Improved Handler for full logic */
static void mock_service_handler_full(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out)
{
    (void) len;
    ctx->config->tx_buffer[0] = data[0] | 0x40u;
    uds_ok(out, 1u);
}

/* Re-bind in setup */
static const uds_service_entry_t g_user_services_full[] = {
    {0x11, 1, UDS_SESSION_ALL, 0, mock_service_handler_full, NULL, 0u},
    {0xA0, 1, UDS_SESSION_ALL, 0, mock_service_handler_full, NULL, 0u},
};

static int setup_full(void **state)
{
    (void) state;
    setup_ctx(&g_ctx, &g_cfg);
    g_cfg.get_time_ms = my_get_time;
    g_cfg.fn_is_safe = mock_is_safe;
    g_cfg.user_services = g_user_services_full;
    g_cfg.user_service_count = 2;
    g_safe_state = true;
    return 0;
}

static void test_pass(void **state)
{
    (void) state;
    g_safe_state = true;
    uint8_t req[] = {0x11, 0x01};
    uint8_t expected[] = {0x51}; /* Positive Response */

    expect_memory(mock_tp_send, data, expected, 1);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_safety_check_fails, setup_full, teardown),
        cmocka_unit_test_setup_teardown(test_safety_check_passes, setup_full, teardown),
        cmocka_unit_test_setup_teardown(test_pass, setup_full, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
