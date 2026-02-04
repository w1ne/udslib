/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) subfn;
    (void) max_len;
    out_buf[0] = 0xAA;
    return 1;
}

static int mock_mem_write(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data)
{
    (void) ctx;
    (void) addr;
    (void) size;
    (void) data;
    return 0;
}

static int mock_dtc_clear(struct uds_ctx *ctx, uint32_t group)
{
    (void) ctx;
    (void) group;
    return 0;
}

static int mock_comm_control(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type)
{
    (void) ctx;
    (void) ctrl_type;
    (void) comm_type;
    return 0;
}

/* 1. Test 0x19 (ReadDTCInfo) Suppression */
static void test_dtc_read_suppression(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read;

    /* 0x99 = 0x19 | 0x80 (Suppress). Sub 0x01 requires a mask (3 bytes total). */
    uint8_t request[] = {0x19, 0x81, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* Expect NO send_tp */

    uds_input_sdu(&ctx, request, sizeof(request));
}

/* 2. Test 0x3D (WriteMemoryByAddress) Response Echo */
static void test_write_mem_echo(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_write = mock_mem_write;

    /* format 0x11: addr_len=1, size_len=1. addr=0xAA, size=0x01, data=0xBB */
    uint8_t request[] = {0x3D, 0x11, 0xAA, 0x01, 0xBB};

    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 0x7D 0x11 0xAA 0x01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7D);
    assert_int_equal(g_tx_buf[1], 0x11);
    assert_int_equal(g_tx_buf[2], 0xAA);
    assert_int_equal(g_tx_buf[3], 0x01);
}

/* 3. Test 0x14 (ClearDTC) with optional memory selection */
static void test_clear_dtc_mem_selection(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_clear = mock_dtc_clear;

    /* 0x14 FF FF FF + 0x01 (Memory selection) */
    uint8_t request[] = {0x14, 0xFF, 0xFF, 0xFF, 0x01};

    will_return(mock_get_time, 3000);
    will_return(mock_get_time, 3000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));
}

/* 4. Test 0x28 (CommControl) subfunctions up to 0x05 */
static void test_comm_control_expanded(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_comm_control = mock_comm_control;

    uint8_t request[] = {0x28, 0x04, 0x01}; /* 0x04 = enableRxAndDisableTx */

    will_return(mock_get_time, 4000);
    will_return(mock_get_time, 4000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));
    assert_int_equal(ctx.comm_state, 0x04);
}

/* 5. Test TesterPresent during Busy */
static void test_busy_tester_present(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* Simulate pending request */
    ctx.p2_msg_pending = true;
    ctx.pending_sid = 0x22;

    /* Suppressed TesterPresent should be IGNORED (no NRC 0x21) */
    uint8_t tp_req[] = {0x3E, 0x80};
    will_return(mock_get_time, 5000);
    /* Expect NO send_tp */
    uds_input_sdu(&ctx, tp_req, sizeof(tp_req));

    assert_true(ctx.p2_msg_pending); /* Still pending original */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dtc_read_suppression),
        cmocka_unit_test(test_write_mem_echo),
        cmocka_unit_test(test_clear_dtc_mem_selection),
        cmocka_unit_test(test_comm_control_expanded),
        cmocka_unit_test(test_busy_tester_present),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
