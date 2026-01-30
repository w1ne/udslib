#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Integration Test Context */
static uds_ctx_t g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buf[4096];
static uint8_t g_tx_buf[4096];

/* Mock Transport "Network" */
static uint8_t g_network_buf[4096];
static uint16_t g_network_len = 0;

static int mock_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    if (len > sizeof(g_network_buf)) return -1;
    memcpy(g_network_buf, data, len);
    g_network_len = len;
    return 0;
}

static uint32_t g_time_ms = 0;
static uint32_t mock_get_time(void) { return g_time_ms; }

/* Mock Helpers */
static int mock_did_write(struct uds_ctx *ctx, uint16_t did, const uint8_t *data, uint16_t len) {
    return 0; /* Success */
}

static const uds_did_entry_t g_dids[] = {
    {0xF190, 17, NULL, NULL, (void*)"VIN12345678901234"}, /* VIN */
    {0x0100, 4, NULL, mock_did_write, NULL},
};

static const uds_did_table_t g_did_table = { g_dids, 2 };

static int setup(void **state) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_cfg, 0, sizeof(g_cfg));
    
    g_cfg.get_time_ms = mock_get_time;
    g_cfg.fn_tp_send = mock_tp_send;
    g_cfg.rx_buffer = g_rx_buf;
    g_cfg.rx_buffer_size = sizeof(g_rx_buf);
    g_cfg.tx_buffer = g_tx_buf;
    g_cfg.tx_buffer_size = sizeof(g_tx_buf);
    g_cfg.did_table = g_did_table;
    
    uds_init(&g_ctx, &g_cfg);
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* Helper to simulate receiving frame */
static void send_to_uds(const uint8_t *data, uint16_t len) {
    uds_input_sdu(&g_ctx, data, len);
}

static void verify_response(const uint8_t *expected, uint16_t len) {
    assert_int_equal(g_network_len, len);
    assert_memory_equal(g_network_buf, expected, len);
    /* Reset network */
    g_network_len = 0;
    memset(g_network_buf, 0, sizeof(g_network_buf));
}

/* Full Lifecycle Test */
static void test_full_lifecycle(void **state) {
    /* 1. Default Session Check */
    /* Read VIN (F190) */
    uint8_t req_vin[] = {0x22, 0xF1, 0x90};
    send_to_uds(req_vin, sizeof(req_vin));
    
    /* Expect 62 F1 90 + 17 bytes VIN */
    uint8_t resp_vin[20] = {0x62, 0xF1, 0x90};
    memcpy(&resp_vin[3], "VIN12345678901234", 17);
    verify_response(resp_vin, 20);
    
    /* 2. Enter Extended Session (0x10 0x03) */
    uint8_t req_sess[] = {0x10, 0x03};
    send_to_uds(req_sess, sizeof(req_sess));
    
    uint8_t resp_sess[] = {0x50, 0x03, 0x00, 0x32, 0x01, 0xF4};
    verify_response(resp_sess, 6);
    
    assert_int_equal(g_ctx.active_session, 0x03);
    
    /* 3. Security Access (0x27 0x01) */
    uint8_t req_seed[] = {0x27, 0x01};
    send_to_uds(req_seed, sizeof(req_seed));
    
    /* Seed is mocked in uds_service_security.c as DE AD BE EF */
    uint8_t resp_seed[] = {0x67, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
    verify_response(resp_seed, 6);
    
    /* 4. Unlock (0x27 0x02 + Key) */
    /* Key DF AE BF F0 */
    uint8_t req_key[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};
    send_to_uds(req_key, sizeof(req_key));
    
    uint8_t resp_unlock[] = {0x67, 0x02};
    verify_response(resp_unlock, 2);
    
    assert_int_equal(g_ctx.security_level, 1);
    
    /* 5. Write Data (0x2E 0x01 0x00) */
    /* Requires Security? Assuming 0x2E requires security in core? 
       Check uds_service_security definition? 
       Core service table has Check Security logic. 
       But we didn't set security mask for 0x2E. 
       Core defaults? 0x2E usually requires.
       Let's assume unlocked allows it. */
    
    uint8_t req_write[] = {0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
    send_to_uds(req_write, sizeof(req_write));
    
    uint8_t resp_write[] = {0x6E, 0x01, 0x00};
    verify_response(resp_write, 3);
    
    /* 6. Tester Present (0x3E 0x00) should keep session alive */
    /* Advance time close to timeout */
    g_time_ms += 4000;
    uds_process(&g_ctx);
    assert_int_equal(g_ctx.active_session, 0x03); /* Still Extended */
    
    uint8_t req_tp[] = {0x3E, 0x00};
    send_to_uds(req_tp, sizeof(req_tp));
    
    uint8_t resp_tp[] = {0x7E, 0x00};
    verify_response(resp_tp, 2);
    
    /* 7. Timeout */
    g_time_ms += 6000; /* > 5000ms S3 */
    uds_process(&g_ctx);
    assert_int_equal(g_ctx.active_session, 0x01); /* Reverted to Default */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_full_lifecycle, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
