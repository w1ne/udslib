#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

/* --- Integration Mock Infrastructure --- */

/* Contexts for Sender (Client) and Receiver (Server) */
static struct uds_ctx server_ctx;
static uds_config_t server_config;
static uint8_t server_rx_buffer[4096];

/* We need separate ISOTP context for server? 
   No, current implementation uses a global static `g_isotp_ctx`. 
   This limits us to testing one active node at a time using `uds_tp_isotp_process`.
   However, `uds_isotp_rx_callback` is stateless regarding *which* node calls it, 
   except it checks `g_isotp_ctx.rx_id`.
   
   To test loopback properly with a SINGLE global context, we must:
   1. Configure global context as Client.
   2. Send Data (Client -> Server).
   3. "Network" captures frame.
   4. Reconfigure global context as Server (?) -> Impossible if we want full loop.
   
   Alternative: Test "One Way" Integration.
   We act as Sender. "Network" Mock receives frames and manually calls `uds_isotp_rx_callback` 
   injecting frames *as if* they came from the other side.
   
   Wait, `uds_tp_isotp.c` has `static uds_isotp_ctx_t g_isotp_ctx`.
   This means we CANNOT run two instances of ISO-TP logic (Client & Server) simultaneously 
   in the same process if they both rely on that static global.
   
   So "Loopback" in the sense of Client <-> Server communicating is impossible without refactoring 
   `uds_tp_isotp.c` to not use static global.
   
   Scope for this task: Add CAN-FD support. Refactoring static global is out of scope.
   
   Therefore, Integration Test will define:
   - "Self-Loopback"? No, TX ID != RX ID.
   - We will test: 
     A) Send Flow: `uds_isotp_send` -> Mock Can -> Verify Frames.
     B) Receive Flow: Inject Frames -> `uds_isotp_rx_callback` -> Verify `uds_input_sdu`.
     C) "Simulated Interaction": 
        - Start Send.
        - Mock Can catches FF. 
        - Test Logic manually injects FC (as if from remote).
        - Mock Can catches CFs.
        - Verify all.
        
   This is what we did in `test_tp_flow_control.c` but we can make it more complex for FD.
*/

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    /* In this test, we can capture the output and perform logic */
    /* Checks are done via expect_... in main test */
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return 0;
}

static void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    check_expected(len);
    check_expected_ptr(data);
}

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8); // TX=7E0, RX=7E8
    uds_tp_isotp_set_fd(true);
    
    // Setup server context for RX callbacks (though library doesn't strictly use it for state)
    server_config.rx_buffer = server_rx_buffer;
    server_config.rx_buffer_size = sizeof(server_rx_buffer);
    server_ctx.config = &server_config;
    
    return 0;
}

static int teardown(void **state)
{
    (void) state;
    return 0;
}

/* Integration: Full Transmission of Large Data (FD) with Flow Control */
static void test_integration_large_transfer(void **state)
{
    (void) state;
    
    /* Data: 200 bytes */
    uint8_t tx_data[200];
    for(int i=0; i<200; i++) tx_data[i] = (uint8_t)i;
    
    /* 1. Start Transmission */
    /* FF: [10] [C8] [Data: 0..61 (62 bytes)] */
    /* Total 64 bytes */
    uint8_t expected_ff[64];
    expected_ff[0] = 0x10;
    expected_ff[1] = 0xC8; // 200
    memcpy(&expected_ff[2], tx_data, 62);
    
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_memory(mock_can_send, data, expected_ff, 64);
    
    uds_isotp_send(NULL, tx_data, 200);
    
    /* 2. Inject Flow Control (CTS, BS=0, ST=0) from "Receiver" */
    /* FC: [30] [00] [00] ... */
    uint8_t rx_fc[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(&server_ctx, 0x7E8, rx_fc, 8);
    
    /* 3. Verify Consecutive Frames */
    /* Remaining: 200 - 62 = 138 bytes */
    /* CF1: [21] [Data: 62..124 (63 bytes)] */
    uint8_t expected_cf1[64];
    expected_cf1[0] = 0x21;
    memcpy(&expected_cf1[1], &tx_data[62], 63);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_memory(mock_can_send, data, expected_cf1, 64);

    uds_tp_isotp_process(100);

    /* Remaining: 138 - 63 = 75 bytes */
    /* CF2: [22] [Data: 125..187 (63 bytes)] */
    uint8_t expected_cf2[64];
    expected_cf2[0] = 0x22;
    memcpy(&expected_cf2[1], &tx_data[125], 63);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_memory(mock_can_send, data, expected_cf2, 64);

    uds_tp_isotp_process(100); /* ST=0, so ready immediately? Timer logic resets to time_ms. */
    /* Logic: timer_st = 100. Call with 100. Elapsed=0. If ST=0, OK. */

    /* Remaining: 75 - 63 = 12 bytes */
    /* CF3: [23] [Data: 188..199 (12 bytes)] */
    /* Length: 1 + 12 = 13 bytes. Aligns to 16. */
    uint8_t expected_cf3[16] = {0};
    expected_cf3[0] = 0x23;
    memcpy(&expected_cf3[1], &tx_data[188], 12);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 16);
    expect_memory(mock_can_send, data, expected_cf3, 16);

    uds_tp_isotp_process(100);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_integration_large_transfer, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
