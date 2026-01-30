#include "test_framework.h"
#include "../include/uds/uds_core.h"
#include <string.h>

/* Mock Infrastructure */
static uint8_t mock_tx_buf[1024];
static uint8_t mock_rx_buf[1024];
static int     mock_tx_len = 0;

static uint32_t mock_time_ms = 1000;

uint32_t get_time_mock(void) { return mock_time_ms; }

int tp_send_mock(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    (void)ctx;
    memcpy(mock_tx_buf, data, len);
    mock_tx_len = len;
    return 0;
}

uds_config_t cfg = {
    .get_time_ms = get_time_mock,
    .fn_tp_send = tp_send_mock,
    .rx_buffer = mock_rx_buf,
    .rx_buffer_size = sizeof(mock_rx_buf),
    .tx_buffer = mock_tx_buf,
    .tx_buffer_size = sizeof(mock_tx_buf),
    .fn_log = NULL
};

/* Tests */

int test_init_validation(void) {
    uds_ctx_t ctx;
    
    // 1. Valid Init
    TEST_ASSERT(uds_init(&ctx, &cfg) == UDS_OK);
    
    // 2. Missing Context
    TEST_ASSERT(uds_init(NULL, &cfg) == UDS_ERR_INVALID_ARG);
    
    // 3. Missing Config
    TEST_ASSERT(uds_init(&ctx, NULL) == UDS_ERR_INVALID_ARG);
    
    TEST_PASS();
}

int test_session_transition(void) {
    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);
    mock_tx_len = 0;
    
    // Input: Session Control (10) Extended (03)
    uint8_t req[] = {0x10, 0x03};
    uds_input_sdu(&ctx, req, sizeof(req));
    
    // Verification:
    // 1. Should have sent response (Len > 0)
    TEST_ASSERT(mock_tx_len > 0);
    
    // 2. Response SID should be 0x50
    TEST_ASSERT(mock_tx_buf[0] == 0x50);
    
    // 3. Sub-function should be 0x03
    TEST_ASSERT(mock_tx_buf[1] == 0x03);
    
    TEST_PASS();
}

int test_invalid_service(void) {
    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);
    mock_tx_len = 0;
    
    // Input: Invalid SID (FE)
    uint8_t req[] = {0xFE};
    uds_input_sdu(&ctx, req, sizeof(req));
    
    // Expected: NRC (7F FE 11)
    TEST_ASSERT(mock_tx_len == 3);
    TEST_ASSERT(mock_tx_buf[0] == 0x7F);
    TEST_ASSERT(mock_tx_buf[1] == 0xFE);
    TEST_ASSERT(mock_tx_buf[2] == 0x11); // Service Not Supported
    
    TEST_PASS();
}

int main(void) {
    printf("--- LibUDS Unit Tests ---\n");
    run_test(test_init_validation, "Initialization Validation");
    run_test(test_session_transition, "Session Control Transition");
    run_test(test_invalid_service, "Invalid Service Handling");
    return 0;
}
