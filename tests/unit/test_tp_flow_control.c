/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

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

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[1024];

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    return 0;
}

static int teardown(void **state)
{
    (void) state;
    return 0;
}

/* 1. Verify STmin Enforcement */
static void test_tp_stmin_enforcement(void **state)
{
    (void) state;
    /* Send First Frame for 20 bytes (requires 1 FF + 2 CF) */
    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    uint8_t expected_ff[] = {0x10, 0x14, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);

    uds_isotp_send(&g_iso, data, 20);

    /* Receive FC (CTS, BS=0, STmin=50ms) */
    uint8_t fc_frame[] = {0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00};  // 0x32 = 50ms
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_frame, 8);

    /* Process at T=0. Should NOT send CF because STmin might need a baseline.
       Actually, the first CF after FC should probably be sent immediately or wait?
       Standard says STmin is between consecutive frames.
       UDSLib implementation resets timer_st on send.
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

    uds_tp_isotp_process(&g_iso, 100); /* Send first CF */

    /* Process at T=120 (Elapsed=20ms). Should NOT send next CF (STmin=50). */
    uds_tp_isotp_process(&g_iso, 120);

    /* Process at T=155 (Elapsed=55ms). SHOULD send next CF. */
    uint8_t expected_cf2[] = {0x22, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf2, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(&g_iso, 155);
}

/* 2. Verify Block Size (BS) Enforcement */
static void test_tp_bs_enforcement(void **state)
{
    (void) state;
    /* Send First Frame for 30 bytes (FF + 4 CF) */
    uint8_t data[30];
    memset(data, 0xBB, sizeof(data));

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_send(&g_iso, data, 30);

    /* Receive FC (CTS, BS=2, STmin=0ms) */
    uint8_t fc_frame[] = {0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_frame, 8);

    /* Send CF 1 */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(&g_iso, 200);

    /* Send CF 2 (BS limit reached) */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(&g_iso, 201);

    /* Process again. Should be in ISOTP_TX_WAIT_FC. No CF sent. */
    uds_tp_isotp_process(&g_iso, 202);

    /* Receive another FC (CTS, BS=0, STmin=0) */
    uint8_t fc_frame2[] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_frame2, 8);

    /* Now it should send remaining 2 CFs */
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(&g_iso, 300);

    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_tp_isotp_process(&g_iso, 301);
}

/* 3. Verify duplex mode default and setter */
static void test_tp_duplex_mode_default_and_set(void **state)
{
    (void) state;
    /* g_iso is freshly initialized by setup() */
    assert_int_equal(g_iso.mode, ISOTP_HALF_DUPLEX); /* default */

    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    assert_int_equal(g_iso.mode, ISOTP_FULL_DUPLEX);

    uds_tp_isotp_set_mode(&g_iso, ISOTP_HALF_DUPLEX);
    assert_int_equal(g_iso.mode, ISOTP_HALF_DUPLEX);

    uds_tp_isotp_set_mode(NULL, ISOTP_FULL_DUPLEX); /* must not crash */
}

/* 4. First CF after FC.CTS must be sent immediately regardless of STmin.
 *
 * ISO 15765-2 §6.5.5.5: STmin governs the minimum time between two successive
 * Consecutive Frames (CF_n-1 → CF_n). There is no preceding CF before the
 * first one, so it must be sent on the very next process() tick after CTS is
 * received, even when time elapsed since FC reception is less than STmin.
 *
 * Bug reproduced: uds_rx_fc() did not initialise iso->timer_st on the CTS
 * path, so `elapsed = time_ms - timer_st` evaluated with timer_st==0 and a
 * small time_ms gave elapsed < STmin, blocking the first CF.
 */
static void test_first_cf_sent_immediately_after_cts(void **state)
{
    (void) state;

    /* Build a 20-byte payload: needs 1 FF + 2 CFs. */
    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));

    /* --- Step 1: Send FF (expect exactly one CAN frame out) --- */
    uint8_t expected_ff[] = {0x10, 0x14, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_ff, 8);
    will_return(mock_can_send, 0);

    uds_isotp_send(&g_iso, data, 20);

    /* Verify we entered WAIT_FC state. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* --- Step 2: Deliver FC.CTS with STmin = 0x32 (50 ms), BS = 0 --- */
    /* The FC arrives at some small wall-clock value; the exact value does not
       matter for the bug — what matters is that the next process() call uses a
       time_ms < STmin away from CTS receipt time. */
    uint8_t fc_frame[] = {0x30, 0x00, 0x32, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, fc_frame, 8);

    /* Verify we transitioned to SENDING_CF state. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_SENDING_CF);
    assert_int_equal(g_iso.tx_st_min, 0x32); /* STmin latched */

    /* --- Step 3: process() at T=5 ms (only 5 ms since init/FC; << 50 ms STmin).
     *
     * CORRECT behaviour (ISO 15765-2): first CF sent immediately.
     * BUGGY behaviour: elapsed = 5 - 0 = 5 < 50 → CF withheld.
     */
    uint8_t expected_cf1[] = {0x21, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf1, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(&g_iso, 5); /* Must send CF1 — not block on STmin */

    /* --- Step 4: STmin is still enforced BETWEEN successive CFs.
     *
     * At T=10 ms: only 5 ms since CF1 was sent at T=5; must NOT send CF2.
     */
    uds_tp_isotp_process(&g_iso, 10); /* Must NOT send CF2 here */

    /* --- Step 5: At T=60 ms: 55 ms since CF1 (>= 50 ms STmin) → send CF2 --- */
    uint8_t expected_cf2[] = {0x22, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_cf2, 8);
    will_return(mock_can_send, 0);

    uds_tp_isotp_process(&g_iso, 60); /* Must send CF2 now */

    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE); /* Transfer complete */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_tp_stmin_enforcement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_bs_enforcement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tp_duplex_mode_default_and_set, setup, teardown),
        cmocka_unit_test_setup_teardown(test_first_cf_sent_immediately_after_cts, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
