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
int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len) {
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int)mock();
}

/* Mock Input SDU (callback for received data) */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    check_expected_ptr(data);
    check_expected(len);
}

static int setup(void **state) {
    uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8);
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* --- Tests --- */

/* 1. Send Single Frame (SF) */
static void test_send_sf(void **state) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t expected_frame[] = {0x03, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_frame, 8);
    will_return(mock_can_send, 0);

    int ret = uds_isotp_send(NULL, data, 3);
    assert_int_equal(ret, 0);
}

/* 2. Send First Frame (FF) */
static void test_send_ff(void **state) {
    uint8_t data[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    uint8_t expected_ff[] = {0x10, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);

    int ret = uds_isotp_send(NULL, data, 10);
    assert_int_equal(ret, 0);
    /* Note: State is now ISOTP_TX_WAIT_FC */
}

/* 3. Receive Flow Control (FC) and Send Consecutive Frames (CF) */
static void test_recv_fc_send_cf(void **state) {
    /* Prereq: Send FF first */
    uint8_t data[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    uint8_t expected_ff[] = {0x10, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 10);

    /* Now Receive FC (CTS, BlockSize=0, STmin=0) */
    /* FC Frame: 30 00 00 ... */
    uint8_t fc_frame[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    /* Expect CF to be sent immediately after processing FC because STmin=0 */
    /* Remaining data: 0x07, 0x08, 0x09, 0x0A (4 bytes) */
    /* CF Frame: 21 07 08 09 0A 00 00 00 */
    uint8_t expected_cf[] = {0x21, 0x07, 0x08, 0x09, 0x0A, 0x00, 0x00, 0x00}; // Note: padding from g_pending_tx_sdu init? No, usage copies from g_pending_tx_sdu which is populated. 
    // g_pending_tx_sdu is static, but memcpy uses exact content. 
    // Wait, uds_tp_isotp.c:103 sends 8 bytes always?
    // uds_tp_isotp.c:105 uds_internal_tp_send_frame(..., frame, 8)
    // frame is init to 0.
    // memcpy copies 'to_copy' bytes. remaining is 4. to_copy=4.
    // So frame[1..4] = data. frame[5..7] = 0.
    // Correct.

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf, 8);
    will_return(mock_can_send, 0);

    /* Call RX Callback with FC */
    uds_isotp_rx_callback(NULL, 0x7E8, fc_frame, 8);
    
    /* Call process to send CF */
    uds_tp_isotp_process(0);
}

/* 4. Receive Single Frame (SF) */
static void test_recv_sf(void **state) {
    struct uds_ctx dummy_ctx;
    uint8_t sf_frame[] = {0x03, 0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x00, 0x00};
    uint8_t expected_payload[] = {0xAA, 0xBB, 0xCC};

    expect_memory(__wrap_uds_input_sdu, data, expected_payload, 3);
    expect_value(__wrap_uds_input_sdu, len, 3);

    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, sf_frame, 8);
}

/* 5. Receive Multi-Frame (FF + CF) */
static void test_recv_multiframe(void **state) {
    struct uds_ctx dummy_ctx;
    uds_config_t config = {0};
    uint8_t rx_buffer[20];
    config.rx_buffer = rx_buffer;
    config.rx_buffer_size = 20;
    dummy_ctx.config = &config;

    /* FF: Length = 10 (0x00A) */
    /* Frame: 10 0A 01 02 03 04 05 06 */
    uint8_t ff_frame[] = {0x10, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    /* Expect FC (CTS) to be sent */
    /* FC: 30 08 00 ... (BlockSize=8, STmin=0 from params) */
    uint8_t expected_fc[] = {0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    expect_value(mock_can_send, id, 0x7E0); // TX ID (Server -> Client? depends on init. Init passed 7E0 as TX)
    // uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8) -> TX=7E0
    // So if we receive, we respond on TX ID 7E0. Correct.
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_fc, 8);
    will_return(mock_can_send, 0);

    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, ff_frame, 8);

    /* CF: SN=1. Data: 07 08 09 0A */
    /* Frame: 21 07 08 09 0A ... */
    uint8_t cf_frame[] = {0x21, 0x07, 0x08, 0x09, 0x0A, 0x00, 0x00, 0x00};
    uint8_t expected_total[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};

    /* Expect uds_input_sdu when reassembly complete */
    expect_memory(__wrap_uds_input_sdu, data, expected_total, 10);
    expect_value(__wrap_uds_input_sdu, len, 10);

    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, cf_frame, 8);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_send_sf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_send_ff, setup, teardown),
        cmocka_unit_test_setup_teardown(test_recv_fc_send_cf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_recv_sf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_recv_multiframe, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
