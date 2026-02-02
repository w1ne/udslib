#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* --- Local Mocks --- */
static uint8_t g_tx_buf[1024];
static uint8_t g_rx_buf[1024];

static uint32_t mock_get_time(void)
{
    return 1000; /* Always return constant time to avoid starvation */
}

static int mock_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    fprintf(stderr, "[DEBUG] mock_tp_send: len=%d, tx_buf_size=%d\n", len,
            ctx->config->tx_buffer_size);
    for (int i = 0; i < len; i++) fprintf(stderr, "%02X ", data[i]);
    fprintf(stderr, "\n");
    fflush(stderr);

    check_expected_ptr(data);
    check_expected(len);
    memcpy(g_tx_buf, data, len);
    return (int) mock();
}

static uds_ctx_t ctx;
static uds_config_t cfg;

static int setup(void **state)
{
    (void) state;
    memset(&ctx, 0, sizeof(uds_ctx_t));
    memset(&cfg, 0, sizeof(uds_config_t));
    memset(g_tx_buf, 0, sizeof(g_tx_buf));
    memset(g_rx_buf, 0, sizeof(g_rx_buf));

    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;

    uds_init(&ctx, &cfg);
    return 0;
}

/* --- Service Mocks --- */
static int mock_did_large_read(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    (void) max_len;
    /* Fill with dummy data */
    memset(buf, 0xAA, 100);
    return 100;
}

static int mock_did_error_read(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    (void) buf;
    (void) max_len;
    /* Simulate Security Access Denied */
    return -0x33;
}

static int mock_comm_control(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type)
{
    (void) ctx;
    (void) ctrl_type;
    (void) comm_type;
    return 0;
}

/* --- Tests --- */

/* 1. Test 0x22 Overflow (Response Too Long) */
static void test_did_overflow(void **state)
{
    (void) state;

    /* Setup 3 DIDs of 100 bytes each. Tx buffer is 256. 3*100 + 3*2 (IDs) + 1 (SID) = 307 > 256 */
    static const uds_did_entry_t dids[] = {
        {0x1234, 100, UDS_SESSION_ALL, 0, mock_did_large_read, NULL, NULL},
        {0x5678, 100, UDS_SESSION_ALL, 0, mock_did_large_read, NULL, NULL},
        {0x9ABC, 100, UDS_SESSION_ALL, 0, mock_did_large_read, NULL, NULL},
    };
    static const uds_did_table_t table = {dids, 3};
    cfg.did_table = table;

    uint8_t req[] = {0x22, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    /* Force buffer size to 256 to trigger overflow (307 > 256) */
    cfg.tx_buffer_size = 256;

    /* Expect NRC 0x14 */
    uint8_t resp[] = {0x7F, 0x22, 0x14};
    expect_value(mock_tp_send, len, 3);
    expect_memory(mock_tp_send, data, resp, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 2. Test 0x22 Specific Error Code from Callback */
static void test_did_specific_nrc(void **state)
{
    (void) state;

    static const uds_did_entry_t dids[] = {
        {0x1234, 10, UDS_SESSION_ALL, 0, mock_did_error_read, NULL, NULL},
    };
    static const uds_did_table_t table = {dids, 1};
    cfg.did_table = table;

    uint8_t req[] = {0x22, 0x12, 0x34};

    /* Expect NRC 0x33 from callback (-0x33) */
    uint8_t resp[] = {0x7F, 0x22, 0x33};
    expect_value(mock_tp_send, len, 3);
    expect_memory(mock_tp_send, data, resp, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 3. Test 0x28 Invalid Communication Type */
static void test_comm_control_invalid_type(void **state)
{
    (void) state;
    cfg.fn_comm_control = mock_comm_control;

    /* 0x28, Control=0x00, CommType=0x00 (Invalid, lower nibble 0) */
    uint8_t req[] = {0x28, 0x00, 0x00};

    /* Expect NRC 0x31 */
    uint8_t resp[] = {0x7F, 0x28, 0x31};
    expect_value(mock_tp_send, len, 3);
    expect_memory(mock_tp_send, data, resp, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 4. Test 0x28 Valid Communication Type */
static void test_comm_control_valid_type(void **state)
{
    (void) state;
    cfg.fn_comm_control = mock_comm_control;

    /* 0x28, Control=0x00, CommType=0x01 (Valid) */
    uint8_t req[] = {0x28, 0x00, 0x01};

    /* Expect Positive Response */
    uint8_t resp[] = {0x68, 0x00};
    expect_value(mock_tp_send, len, 2);
    expect_memory(mock_tp_send, data, resp, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_did_overflow, setup),
        cmocka_unit_test_setup(test_did_specific_nrc, setup),
        cmocka_unit_test_setup(test_comm_control_invalid_type, setup),
        cmocka_unit_test_setup(test_comm_control_valid_type, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
