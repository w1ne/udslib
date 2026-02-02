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
    /* Reject SID 0x11 (Reset) if g_safe_state is false */
    if (sid == 0x11 && !g_safe_state) {
        return false;
    }
    return true; /* Allow everything else */
}

/* Mock Service Handler */
static int mock_service_handler(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    /* Send a dummy positive response */
    uint8_t resp[] = {data[0] | 0x40, 0x00};
    return uds_send_response(ctx, 2); /* This will call mock_tp_send */
}

static const uds_service_entry_t g_user_services[] = {
    {0x11, 1, UDS_SESSION_ALL, 0, mock_service_handler}, /* Dummy logic for 0x11 to avoid calling real one */
    {0xA0, 1, UDS_SESSION_ALL, 0, mock_service_handler},
};

/* Local time provider override */
static uint32_t my_get_time(void) { return 0; }

static int setup(void **state) {
    setup_ctx(&g_ctx, &g_cfg);
    g_cfg.get_time_ms = my_get_time;
    g_cfg.fn_is_safe = mock_is_safe;
    
    /* Override core services with dummies to isolate safety check */
    g_cfg.user_services = g_user_services;
    g_cfg.user_service_count = 2;
    
    g_safe_state = true;
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* 1. Test Unsafe Condition (NRC 0x22) */
static void test_safety_check_fails(void **state) {
    g_safe_state = false;
    uint8_t req[] = {0x11, 0x01}; /* Reset */

    /* Expect NRC 7F 11 22 (ConditionsNotCorrect) */
    uint8_t expected[] = {0x7F, 0x11, 0x22};

    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

/* 2. Test Safe Condition (Success) */
static void test_safety_check_passes(void **state) {
    g_safe_state = true;
    uint8_t req[] = {0x11, 0x01}; 

    /* Expect Positive Response 51 00 (0x11|0x40 = 0x51) */
    /* mock_service_handler sends 2 bytes (SID+40, 00) ? 
       No, uds_send_response uses ctx->config->tx_buffer.
       We should set tx_buffer[0], tx_buffer[1].
       Wait, mock_service_handler uses uds_send_response.
       It should write to tx_buffer first.
    */
    /* Correct mock handler logic: */
    /* Handled by replacement below? No, I need to fix mock_service_handler logic in previous step or here */
    /* Actually, uds_send_response sends whatever is in tx_buffer. header 0x51 is usually set by handler. */
}

/* Improved Handler for full logic */
static int mock_service_handler_full(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    ctx->config->tx_buffer[0] = data[0] | 0x40;
    return uds_send_response(ctx, 1);
}

/* Re-bind in setup */
static const uds_service_entry_t g_user_services_full[] = {
    {0x11, 1, UDS_SESSION_ALL, 0, mock_service_handler_full}, 
    {0xA0, 1, UDS_SESSION_ALL, 0, mock_service_handler_full},
};

static int setup_full(void **state) {
    setup_ctx(&g_ctx, &g_cfg);
    g_cfg.get_time_ms = my_get_time;
    g_cfg.fn_is_safe = mock_is_safe;
    g_cfg.user_services = g_user_services_full;
    g_cfg.user_service_count = 2;
    g_safe_state = true;
    return 0;
}

static void test_pass(void **state) {
    g_safe_state = true;
    uint8_t req[] = {0x11, 0x01};
    uint8_t expected[] = {0x51}; /* Positive Response */
    
    expect_memory(mock_tp_send, data, expected, 1);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    
    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_safety_check_fails, setup_full, teardown),
        cmocka_unit_test_setup_teardown(test_pass, setup_full, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
