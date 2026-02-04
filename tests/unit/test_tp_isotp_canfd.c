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
static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int) mock();
}

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8);
    // Explicitly enable FD
    uds_tp_isotp_set_fd(true);
    return 0;
}

static int teardown(void **state)
{
    (void) state;
    return 0;
}

/* 1. Verify CAN-FD Single Frame (> 8 bytes) */
static void test_tp_canfd_sf(void **state)
{
    (void) state;
    /* Send 12 bytes. Should be a single frame in FD (max 62) */
    uint8_t data[12];
    memset(data, 0xCC, sizeof(data));

    uint8_t expected_frame[14]; /* 2 header + 12 data */
    expected_frame[0] = 0x00;   /* SF */
    expected_frame[1] = 12;     /* DL */
    memcpy(&expected_frame[2], data, 12);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 16); /* 14 bytes snapped to 16 */
    /* Note: We expect padded bytes to be 0 or garbage?
       Implementation memset to 0 initially.
       But we only copy 14 bytes.
       The expect_memory checks exact bytes.
       If we say len=16, mock_can_send likely checks 16 bytes.
       We initialized expected_frame to size 14. We need to resize it.
    */
    uint8_t expected_frame_aligned[16] = {0};
    expected_frame_aligned[0] = 0x00;
    expected_frame_aligned[1] = 12;
    memcpy(&expected_frame_aligned[2], data, 12);

    expect_memory(mock_can_send, data, expected_frame_aligned, 16);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 12);
}

/* 2. Verify CAN-FD First Frame and Consecutive Frame */
static void test_tp_canfd_ff_cf(void **state)
{
    (void) state;
    /* Send 100 bytes.
       SF max is 62. So this must be Multi-frame.
       FF (FD): 2 header + 62 data = 64 bytes.
       Remaining: 100 - 62 = 38 bytes.
       CF (FD): 1 header + 38 data = 39 bytes.
    */
    uint8_t data[100];
    for (int i = 0; i < 100; i++) data[i] = (uint8_t) i;

    /* Expected FF */
    uint8_t expected_ff[64];
    expected_ff[0] = 0x10;             /* FF | DL_Hi=0 */
    expected_ff[1] = 100;              /* DL_Lo */
    memcpy(&expected_ff[2], data, 62); /* 62 bytes of payload */

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_memory(mock_can_send, data, expected_ff, 64);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 100);

    /* Receive FC (CTS) */
    uint8_t fc_frame[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(NULL, 0x7E8, fc_frame, 8);

    /* Expected CF */
    /* Len 39 -> Aligned to 48 */
    uint8_t expected_cf[48] = {0};
    expected_cf[0] = 0x21; /* CF | SN=1 */
    memcpy(&expected_cf[1], &data[62], 38);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 48);
    expect_memory(mock_can_send, data, expected_cf, 48);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(100);
}

/* 3. Verify CAN-FD RX Processing (SF) */
static void test_tp_canfd_rx_sf(void **state)
{
    (void) state;
    struct uds_ctx dummy_ctx;
    uint8_t rx_frame[14];

    /* SF > 8 bytes (12 bytes payload) */
    rx_frame[0] = 0x00;
    rx_frame[1] = 12;
    memset(&rx_frame[2], 0xDD, 12);

    uint8_t expected_payload[12];
    memset(expected_payload, 0xDD, 12);

    expect_memory(__wrap_uds_input_sdu, data, expected_payload, 12);
    expect_value(__wrap_uds_input_sdu, len, 12);

    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, rx_frame, 14);
}

/* 4. Boundary Test: SF exactly 62 bytes (Max SF for FD) */
static void test_tp_canfd_sf_boundary(void **state)
{
    (void) state;
    uint8_t data[62];
    memset(data, 0xEE, 62);

    /* Expect SF: [00] [3E] [Data...] = 64 bytes total */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    /* We could check content but memory check for 64 bytes is verbose.
       Just check length is correct implying fit. */
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 62);
}

/* 5. Boundary Test: Multi-Frame just above SF limit (63 bytes) */
static void test_tp_canfd_mf_boundary(void **state)
{
    (void) state;
    uint8_t data[63];
    memset(data, 0xFF, 63);

    /* FF: [10] [3F] [Data (62 bytes)] -> Full 64 bytes frame */
    /* Remaining: 1 byte */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 64);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);

    uds_isotp_send(NULL, data, 63);

    /* Receive FC */
    uint8_t fc[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(NULL, 0x7E8, fc, 8);

    /* CF: [21] [Data (1 byte)] -> 2 bytes frame usually, padded?
       Our implementation sends len = 1 + remaining.
       So len = 2. */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 2);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(100);
}

/* 6. Verify DLC Alignment Boundaries */
static void test_dlc_alignment_boundaries(void **state)
{
    (void) state;
    uint8_t data[62] = {0};

    // Test 9 bytes -> Aligns to 12
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 12);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 9);  // SF: 2 header + 9 data = 11 -> 12

    // Test 17 bytes -> Aligns to 20
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 20);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 17);  // SF: 2 header + 17 data = 19 -> 20

    // Test 33 bytes -> Aligns to 48
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 48);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 33);  // SF: 2 header + 33 data = 35 -> 48
}

/* 7. Verify Error Handling: Invalid Frames */
static void test_rx_error_cases(void **state)
{
    (void) state;
    uds_ctx_t dummy_ctx;
    uint8_t frame[64] = {0};

    // Case A: SF with length 0 (Invalid)
    frame[0] = 0x00;
    frame[1] = 0x00;
    // Should NOT call uds_input_sdu
    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, frame, 8);

    // Case B: SF with length > DLC
    frame[0] = 0x00;
    frame[1] = 60;  // Claim 60 bytes
    // Actual DLC is 8. Error. Should return.
    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, frame, 8);

    // Case C: FF with length < 8
    frame[0] = 0x10;
    frame[1] = 0x07;  // 7 bytes total length
    // Should be SF. Ignore.
    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, frame, 8);
}

/* 8. Verify State Reset on Interruption */
static void test_state_reset(void **state)
{
    (void) state;
    // Simulate receiving FF
    uint8_t ff[8] = {0x10, 0x14, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};  // Len 20
    uds_ctx_t dummy_ctx;
    uds_config_t config;
    uint8_t buffer[64];
    dummy_ctx.config = &config;
    config.rx_buffer = buffer;
    config.rx_buffer_size = 64;

    // FF received -> State becomes RX_WAIT_CF
    expect_value(mock_can_send, id, 0x7E0);  // FC
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, ff, 8);

    // Send unexpected SF
    uint8_t sf[8] = {0x03, 0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x00, 0x00};
    uint8_t expected_sdu[] = {0xAA, 0xBB, 0xCC};

    // Should abort FF reception and process SF
    expect_memory(__wrap_uds_input_sdu, data, expected_sdu, 3);
    expect_value(__wrap_uds_input_sdu, len, 3);
    uds_isotp_rx_callback(&dummy_ctx, 0x7E8, sf, 8);
}

/* 9. Verify Mixed Mode Switching */
static void test_mixed_fd_std(void **state)
{
    (void) state;
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Mode: FD (from setup). Send 8 bytes.
    // SF FD: [00] [08] [Data...] -> 10 bytes -> Align 12
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 12);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 8);

    // Switch to Classic CAN
    uds_tp_isotp_set_fd(false);

    // Send same 8 bytes.
    // Must be Multi-Frame bc SF max is 7 in Std.
    // FF: [10] [08] [Data(6)] -> 8 bytes
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(NULL, data, 8);

    // Set back to FD for teardown consistency
    uds_tp_isotp_set_fd(true);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_tp_canfd_sf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_canfd_ff_cf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_canfd_rx_sf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_canfd_sf_boundary, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_canfd_mf_boundary, setup, teardown),
        cmocka_unit_test_setup_teardown(test_dlc_alignment_boundaries, setup, teardown),
        cmocka_unit_test_setup_teardown(test_rx_error_cases, setup, teardown),
        cmocka_unit_test_setup_teardown(test_state_reset, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mixed_fd_std, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
