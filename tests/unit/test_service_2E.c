#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "test_helpers.h"

static uint8_t g_storage_did[4] = {0x00, 0x00, 0x00, 0x00};

/* Mock Write Callback */
static int mock_did_write_fn(uds_ctx_t *ctx, uint16_t did, const uint8_t *data, uint16_t len)
{
    if (did == 0x1234) {
        /* Fail if data is 0xFF */
        if (data[0] == 0xFF) return -1;
        return 0;
    }
    return -1;
}

static const uds_did_entry_t g_test_dids[] = {
    {0xF00D, 4, UDS_SESSION_ALL, 0, NULL, NULL, g_storage_did}, /* Storage DID */
    {0x1234, 2, UDS_SESSION_ALL, 0, NULL, mock_did_write_fn, NULL}, /* Callback DID */
};

static const uds_did_table_t g_test_table = {
    .entries = g_test_dids,
    .count = 2
};

static uds_ctx_t g_ctx;
static uds_config_t g_cfg;

/* Local time provider to avoid mock queue issues */
static uint32_t my_get_time(void) { return 0; }

static int setup(void **state) {
    setup_ctx(&g_ctx, &g_cfg);
    g_cfg.did_table = g_test_table;
    g_cfg.get_time_ms = my_get_time; /* Override */
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* 1. Test Success Write to Storage */
static void test_wdbi_storage_success(void **state) {
    uint8_t req[] = {0x2E, 0xF0, 0x0D, 0xDE, 0xAD, 0xBE, 0xEF};
    
    /* Expect Response: 6E F0 0D */
    uint8_t expected[] = {0x6E, 0xF0, 0x0D};
    
    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
    
    uint8_t expected_storage[] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert_memory_equal(g_storage_did, expected_storage, 4);
}

/* 2. Test Success Write Callback */
static void test_wdbi_callback_success(void **state) {
    uint8_t req[] = {0x2E, 0x12, 0x34, 0x00, 0x00};
    
    uint8_t expected[] = {0x6E, 0x12, 0x34};
    
    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

/* 3. Test Unknown DID (NRC 0x31) */
static void test_wdbi_unknown_did(void **state) {
    uint8_t req[] = {0x2E, 0xFF, 0xFF, 0x00};
    
    /* Expect NRC 7F 2E 31 */
    uint8_t expected[] = {0x7F, 0x2E, 0x31};
    
    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

/* 4. Test Invalid Length (NRC 0x13) */
static void test_wdbi_invalid_length(void **state) {
    (void)state;
    /* DID F00D expects 4 bytes data. Send 3. Total len = 3(SID+DID) + 3 = 6. Expected 7. */
    uint8_t req[] = {0x2E, 0xF0, 0x0D, 0xAA, 0xBB, 0xCC};
    
    /* Expect NRC 7F 2E 13 (Incorrect Length) */
    uint8_t expected[] = {0x7F, 0x2E, 0x13};
    
    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

/* 5. Test Write Failure (Callback returns -1) (NRC 0x22) */
static void test_wdbi_write_fail(void **state) {
    (void)state;
    /* DID 1234 fails if data[0] == 0xFF */
    uint8_t req[] = {0x2E, 0x12, 0x34, 0xFF, 0x00};
    
    /* Expect NRC 7F 2E 22 (Conditions Not Correct) */
    uint8_t expected[] = {0x7F, 0x2E, 0x22};
    
    expect_memory(mock_tp_send, data, expected, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&g_ctx, req, sizeof(req));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_wdbi_storage_success, setup, teardown),
        cmocka_unit_test_setup_teardown(test_wdbi_callback_success, setup, teardown),
        cmocka_unit_test_setup_teardown(test_wdbi_unknown_did, setup, teardown),
        cmocka_unit_test_setup_teardown(test_wdbi_invalid_length, setup, teardown),
        cmocka_unit_test_setup_teardown(test_wdbi_write_fail, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
