#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "test_helpers.h"

/* Dummy Service Handler */
int mock_service_handler(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    return UDS_OK;
}

/* Setup/Teardown */
static uds_config_t g_config = {0};
static uds_ctx_t g_ctx = {0};

/* Define a test service table */
static const uds_service_entry_t g_test_services[] = {
    /* SID 0xA0: Min Len=2, All Sessions, No Security */
    {0xA0, 2, UDS_SESSION_ALL, 0, mock_service_handler},
    /* SID 0xA1: Min Len=1, Extended(0x02) only, No Security */
    {0xA1, 1, UDS_SESSION_EXTENDED, 0, mock_service_handler},
    /* SID 0xA2: Min Len=1, All Sessions, Security Level 1 needed */
    {0xA2, 1, UDS_SESSION_ALL, 1, mock_service_handler},
};

/* Simple Time Provider (Bypassing CMocka queue) */
uint32_t my_get_time(void) {
    return 0;
}

static int setup(void **state) {
    /* Use helper to setup basic context */
    setup_ctx(&g_ctx, &g_config);

    /* Override with our custom service table */
    g_config.user_services = g_test_services;
    g_config.user_service_count = sizeof(g_test_services) / sizeof(g_test_services[0]);
    
    /* Override time provider to avoid mock() queue issues */
    g_config.get_time_ms = my_get_time;
    
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* Tests */

/* 1. Test NRC 0x11 (ServiceNotSupported) */
static void test_service_not_supported(void **state) {
    uint8_t req[] = {0xFF, 0x01}; /* 0xFF is not in our table */
    
    /* Expect NRC 7F FF 11 */
    uint8_t expected_response[] = {0x7F, 0xFF, 0x11};
    
    expect_memory(mock_tp_send, data, expected_response, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

/* 2. Test NRC 0x13 (IncorrectMessageLength) */
static void test_invalid_length(void **state) {
    /* Service 0xA0 requires min_len=2. Send 1 byte. */
    uint8_t req[] = {0xA0};
    
    /* Expect NRC 7F A0 13 */
    uint8_t expected_response[] = {0x7F, 0xA0, 0x13};
    
    expect_memory(mock_tp_send, data, expected_response, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

/* 3. Test NRC 0x7F (ServiceNotSupportedInActiveSession) */
static void test_session_violation(void **state) {
    /* Default session is 0x01. Service 0xA1 requires Extended (0x02) */
    
    /* Ensure we are in default session */
    g_ctx.active_session = UDS_SESSION_DEFAULT; 
    
    uint8_t req[] = {0xA1};
    
    /* Expect NRC 7F A1 7F */
    uint8_t expected_response[] = {0x7F, 0xA1, 0x7F};

    expect_memory(mock_tp_send, data, expected_response, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

/* 4. Test NRC 0x33 (SecurityAccessDenied) */
static void test_security_violation(void **state) {
    /* Service 0xA2 requires Security Level 1. Context init is Level 0 (Locked). */
    g_ctx.security_level = 0;

    uint8_t req[] = {0xA2};
    
    /* Expect NRC 7F A2 33 */
    uint8_t expected_response[] = {0x7F, 0xA2, 0x33};

    expect_memory(mock_tp_send, data, expected_response, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(uds_input_sdu(&g_ctx, req, sizeof(req)g_ctx, req, sizeof(req, 0));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_service_not_supported, setup, teardown),
        cmocka_unit_test_setup_teardown(test_invalid_length, setup, teardown),
        cmocka_unit_test_setup_teardown(test_session_violation, setup, teardown),
        cmocka_unit_test_setup_teardown(test_security_violation, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
