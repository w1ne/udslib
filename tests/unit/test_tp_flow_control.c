#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

/* Mock CAN Send */
static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len) {
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int)mock();
}

static int setup(void **state) {
    uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8);
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* 1. Verify STmin Enforcement */
static void test_tp_stmin_enforcement(void **state) {
    /* Send First Frame for 20 bytes (requires 1 FF + 2 CF) */
    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    uint8_t expected_ff[] = {0x10, 0x14, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 20);

    /* Receive FC (CTS, BS=0, STmin=50ms) */
    uint8_t fc_frame[] = {0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00}; // 0x32 = 50ms
    uds_isotp_rx_callback(NULL, 0x7E8, fc_frame, 8);

    /* Process at T=0. Should NOT send CF because STmin might need a baseline. 
       Actually, the first CF after FC should probably be sent immediately or wait? 
       Standard says STmin is between consecutive frames. 
       LibUDS implementation resets timer_st on send. 
       If timer_st is 0 initially, elapsed might be large.
    */
    
    /* Current implementation resets timer_st to time_ms on send. 
       So first CF after FC will be sent if elapsed >= st_min.
       Initial timer_st is 0. If we pass time_ms=100, elapsed=100. >= 50. OK.
    */

    uint8_t expected_cf1[] = {0x21, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf1, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(100); /* Send first CF */

    /* Process at T=120 (Elapsed=20ms). Should NOT send next CF (STmin=50). */
    uds_tp_isotp_process(120);

    /* Process at T=155 (Elapsed=55ms). SHOULD send next CF. */
    uint8_t expected_cf2[] = {0x22, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf2, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(155);
}

/* 2. Verify Block Size (BS) Enforcement */
static void test_tp_bs_enforcement(void **state) {
    /* Send First Frame for 30 bytes (FF + 4 CF) */
    uint8_t data[30];
    memset(data, 0xBB, sizeof(data));
    
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 30);

    /* Receive FC (CTS, BS=2, STmin=0ms) */
    uint8_t fc_frame[] = {0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(NULL, 0x7E8, fc_frame, 8);

    /* Send CF 1 */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(200);

    /* Send CF 2 (BS limit reached) */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(201);

    /* Process again. Should be in ISOTP_TX_WAIT_FC. No CF sent. */
    uds_tp_isotp_process(202);

    /* Receive another FC (CTS, BS=0, STmin=0) */
    uint8_t fc_frame2[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(NULL, 0x7E8, fc_frame2, 8);

    /* Now it should send remaining 2 CFs */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(300);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(301);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_tp_stmin_enforcement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_bs_enforcement, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
