#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

static uint32_t mock_time_ms = 0;
static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[256];
static uint8_t tx_buf[256];

/* --- Mocks --- */
static uint32_t mock_get_time(void) {
    return mock_time_ms;
}

static int mock_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    check_expected(data);
    check_expected(len);
    return 0;
}

/* --- Async Service Handler --- */
static int async_service_handler(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    /* Simulate a service that cannot complete immediately */
    return UDS_PENDING;
}

static const uds_service_entry_t user_services[] = {
    {0x31, 4, UDS_SESSION_ALL, 0, async_service_handler},
};

static void setup_test(void **state) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    
    /* Config Timing */
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 2000;

    /* Register Custom Service */
    cfg.user_services = user_services;
    cfg.user_service_count = 1;
    /* Missing table */
    static const uds_did_entry_t dids[] = {{0,0,NULL,NULL,NULL}};
    cfg.did_table.entries = dids;
    cfg.did_table.count = 0;

    uds_init(&ctx, &cfg);
    mock_time_ms = 1000;
}

static void test_async_workflow(void **state) {
    setup_test(state);

    /* 1. Send Request 31 01 FF 00 */
    uint8_t req[] = {0x31, 0x01, 0xFF, 0x00};
    
    /* Expect IMMEDIATE NRC 0x78 because handler returns UDS_PENDING */
    uint8_t exp_nrc78[] = {0x7F, 0x31, 0x78};
    expect_memory(mock_tp_send, data, exp_nrc78, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(uds_input_sdu(&ctx, req, sizeof(req)ctx, req, sizeof(req, 0));

    /* Check State: response_pending should be tracked internally (p2_msg_pending) */
    /* Assert internal state if possible, but struct is opaque/internal. 
       We rely on behavior. */

    /* 2. Advance time < P2* (e.g. 1000ms elapsed). 
       No response expected during process() */
    mock_time_ms += 1000;
    uds_process(&ctx);

    /* 3. Advance time > P2* (e.g. +1100ms = 2100ms total elapsed). 
       Expect another NRC 0x78 (Keep Alive) */
    mock_time_ms += 1100;

    expect_memory(mock_tp_send, data, exp_nrc78, 3);
    expect_value(mock_tp_send, len, 3);

    uds_process(&ctx);

    /* 4. Complete Request Manually */
    /* Hand craft response: 71 01 FF 00 */
    ctx.config->tx_buffer[0] = 0x71;
    ctx.config->tx_buffer[1] = 0x01;
    ctx.config->tx_buffer[2] = 0xFF;
    ctx.config->tx_buffer[3] = 0x00;

    uint8_t exp_pos[] = {0x71, 0x01, 0xFF, 0x00};
    expect_memory(mock_tp_send, data, exp_pos, 4);
    expect_value(mock_tp_send, len, 4);

    uds_send_response(&ctx, 4);

    /* 5. Process again. Should be IDLE. No more 0x78. */
    mock_time_ms += 3000; 
    uds_process(&ctx);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_async_workflow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
