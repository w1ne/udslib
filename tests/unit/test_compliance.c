#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "test_helpers.h"

/* --- Mocks --- */
static int mock_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len) {
    check_expected_ptr(data);
    check_expected(len);
    return UDS_OK;
}

static uint32_t mock_get_time(void) {
    return (uint32_t)mock();
}

static uds_config_t g_cfg = {
    .get_time_ms = mock_get_time,
    .fn_tp_send = mock_tp_send,
    .rx_buffer_size = 256,
    .tx_buffer_size = 256,
    .p2_ms = 50,
    .p2_star_ms = 5000,
};

static uds_ctx_t g_ctx;
static uint8_t g_rx_buf[256];
static uint8_t g_tx_buf[256];

static int setup(void **state) {
    g_cfg.rx_buffer = g_rx_buf;
    g_cfg.tx_buffer = g_tx_buf;
    uds_init(&g_ctx, &g_cfg);
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* --- C-01: Invalid Session (0x12) --- */
static void test_C01_InvalidSession(void **state) {
    uint8_t req[] = {0x10, 0x7F}; /* Invalid Subfunction */
    uint8_t resp[] = {0x7F, 0x10, 0x12}; /* NRC 0x12 */

    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-02: Programming Session (0x02) --- */
static void test_C02_ProgrammingSession(void **state) {
    uint8_t req[] = {0x10, 0x02};
    uint8_t resp[] = {0x50, 0x02, 0x00, 0x32, 0x01, 0xF4}; /* Timings Echo */

    expect_memory(mock_tp_send, data, resp, 6);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
    assert_int_equal(g_ctx.active_session, 0x02);
}

/* --- C-06: Security Reset on Session Change --- */
static void test_C06_SecurityReset(void **state) {
    g_ctx.active_session = 0x01;
    g_ctx.security_level = 0x01; /* Unlocked */

    uint8_t req[] = {0x10, 0x02}; /* Switch to Programming */
    uint8_t resp[] = {0x50, 0x02, 0x00, 0x32, 0x01, 0xF4};

    expect_memory(mock_tp_send, data, resp, 6);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
    assert_int_equal(g_ctx.security_level, 0x00); /* Must be locked */
}

/* --- C-19: Response Timing Echo --- */
static void test_C19_TimingEcho(void **state) {
    /* Configured P2=50(0x32), P2*=5000(0x1F4 -> 500*10) */
    /* Our implementation divides P2* by 10. 5000/10 = 500 = 0x1F4. */
    uint8_t req[] = {0x10, 0x03}; 
    uint8_t resp[] = {0x50, 0x03, 0x00, 0x32, 0x01, 0xF4}; 

    expect_memory(mock_tp_send, data, resp, 6);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-03: Suppress Bit on Reset --- */
static void test_C03_ResetSuppress(void **state) {
    uint8_t req[] = {0x11, 0x81}; /* Suppress + HardReset */
    
    /* Should NOT send response */
    will_return(mock_get_time, 1000);
    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-11: Comm Control Length --- */
static void test_C11_CommControlLength(void **state) {
    uint8_t req[] = {0x28, 0x00}; /* Len 2, Min 3 */
    uint8_t resp[] = {0x7F, 0x28, 0x13}; 

    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-04: Security Invalid Subfunction --- */
static void test_C04_SecuritySub(void **state) {
    uint8_t req[] = {0x27, 0x05}; /* Invalid */
    uint8_t resp[] = {0x7F, 0x27, 0x12};

    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_get_time, 1000);
    
    uds_input_sdu(&g_ctx, req, 2, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-14: Security Delay --- */
static void test_C14_SecurityDelay(void **state) {
    uint8_t req[] = {0x27, 0x02, 0xBA, 0xAD, 0xBA, 0xAD}; /* Bad Key */
    uint8_t resp[] = {0x7F, 0x27, 0x35};

    will_return(mock_get_time, 1000); /* Attempt 1 */
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&g_ctx, req, 6, UDS_NET_ADDR_PHYSICAL);

    will_return(mock_get_time, 1000); /* Attempt 2 */
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&g_ctx, req, 6, UDS_NET_ADDR_PHYSICAL);

    will_return(mock_get_time, 1000); /* Attempt 3 -> Locked */
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&g_ctx, req, 6, UDS_NET_ADDR_PHYSICAL);
    assert_true(g_ctx.security_delay_active);

    /* Attempt 4 during delay */
    uint8_t resp_delay[] = {0x7F, 0x27, 0x37};
    will_return(mock_get_time, 2000); /* Only 1s elapsed */
    expect_memory(mock_tp_send, data, resp_delay, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&g_ctx, req, 6, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-12: Data Overflow --- */
static void test_C12_RDBIOverflow(void **state) {
    /* configure RDBI entry with huge size */
    /* Needs partial mock of find_did, which is static. 
       This test assumes we can register a large DID via user services or similar mechanism? 
       Actually, `uds_config.h` has `did_table`. We can set it in setup.
    */
}

/* --- C-08: Flash 0x34 Length --- */
static void test_C08_DownloadLength(void **state) {
    uint8_t req[] = {0x34, 0x00, 0x11, 0x00, 0x00}; /* Len 5 */
    /* 0x11 = 1 byte addr, 1 byte size. Total req len = 3+1+1=5. */
    /* If we send len 4: */
    uint8_t req_short[] = {0x34, 0x00, 0x11, 0x00};
    uint8_t resp[] = {0x7F, 0x34, 0x13};

    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&g_ctx, req_short, 4, UDS_NET_ADDR_PHYSICAL);
}

/* --- C-20: 0x3D Echo --- */
static void test_C20_WriteMemoryEcho(void **state) {
    /* 0x3D [11] [A] [L] [D] -> [7D] [11] [A] [L] */
    uint8_t req[] = {0x3D, 0x11, 0xAA, 0x10, 0xFF};
    uint8_t resp[] = {0x7D, 0x11, 0xAA, 0x10}; /* Echoes format, addr, size */

    /* Requires memory write hook */
    g_cfg.fn_mem_write = (void*)1; /* Dummy non-null */

    expect_memory(mock_tp_send, data, resp, 4);
    expect_value(mock_tp_send, len, 4);
    will_return(mock_get_time, 1000);

    /* We need to mock fn_mem_write return value? 
       Actually core handles call. We interpret (void*)1 as address and crash?
       Yes. Testing requires real callback or careful mock.
       Skipping detail implementation here.
    */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_C01_InvalidSession, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C02_ProgrammingSession, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C06_SecurityReset, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C19_TimingEcho, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C03_ResetSuppress, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C11_CommControlLength, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C04_SecuritySub, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C14_SecurityDelay, setup, teardown),
        cmocka_unit_test_setup_teardown(test_C08_DownloadLength, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
