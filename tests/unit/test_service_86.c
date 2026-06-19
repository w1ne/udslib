/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

/* Inner serviceToRespondTo target: a DID read. The library runs this when an
 * event fires and wraps its response in the 0xC6 message. */
static int mock_did_read(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    (void) max_len;
    buf[0] = 0xDE;
    buf[1] = 0xAD;
    return 2;
}

static const uds_did_entry_t k_dids[] = {
    {0xF190u, 2u, 0u, 0u, mock_did_read, NULL, NULL},
};

static uds_did_table_t k_did_table = {k_dids, 1u};

/* Setup onChangeOfDataIdentifier(0xF190) -> serviceToRespondTo = 22 F1 90.
 * 86 03 <window=0x02 infinite> <DID=F1 90> <22 F1 90> */
static void test_roe_setup_and_start_acks(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    uint8_t setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* C6 03 <count=1> + echo of request body (data[2..7], 6 bytes) = 3 + 6 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 8);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[2], 0x01); /* numberOfActivatedEvents */

    /* startResponseOnEvent (0x05) */
    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* C6 05 <count> */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);
    assert_int_equal(g_tx_buf[1], 0x05);
}

/* Full flow: setup + start, then trigger emits the captured DID response. */
static void test_roe_trigger_emits(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    uint8_t setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 8);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* DID 0xF190 changed -> emit. Inner 22 F1 90 -> 62 F1 90 DE AD (5 bytes).
     * ROE message: C6 03 <numIdentified=1> + inner(5) = 3 + 5 = 8. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    int emitted = uds_roe_trigger(&ctx, 0x03, 0xF190u);
    assert_int_equal(emitted, 1);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[2], 0x01);
    assert_int_equal(g_tx_buf[3], 0x62); /* inner positive response SID */
    assert_int_equal(g_tx_buf[6], 0xDE);
    assert_int_equal(g_tx_buf[7], 0xAD);
}

/* stop deactivates: a trigger after stop emits nothing. */
static void test_roe_stop_silences(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    uint8_t setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 8);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    uint8_t stop[] = {0x86, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, stop, 2);

    /* No tp_send expected here: event is inactive. */
    int emitted = uds_roe_trigger(&ctx, 0x03, 0xF190u);
    assert_int_equal(emitted, 0);
}

/* Deferred sub-function 0x07 onComparisonOfValues -> NRC 0x12. */
static void test_roe_deferred_subfn(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t req[] = {0x86, 0x07, 0x02, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x12);
}

/* onTimerInterrupt (0x02): once started, the stored service is emitted
 * periodically from uds_process() at the configured rate. */
static void test_roe_timer_fires(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    /* 86 02 <window=0x02 infinite> <rate=0x03 fast> <serviceToRespondTo 22 F1 90> */
    uint8_t setup[] = {0x86, 0x02, 0x02, 0x03, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8); /* C6 02 <count> + echo(5) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 7);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* Tick 1: primes next_fire (= 1000 + 100), no emission yet. */
    will_return(mock_get_time, 1000);
    uds_process(&ctx);

    /* Tick 2: now (1200) >= next_fire (1100) -> emit. */
    will_return(mock_get_time, 1200);
    expect_any(mock_tp_send, data);
    /* C6 02 <numIdentified=1> + inner 62 F1 90 DE AD = 3 + 5 = 8 */
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);

    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_tx_buf[2], 0x01);
    assert_int_equal(g_tx_buf[3], 0x62); /* inner RDBI positive response */
    assert_int_equal(g_tx_buf[6], 0xDE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_roe_setup_and_start_acks), cmocka_unit_test(test_roe_trigger_emits),
        cmocka_unit_test(test_roe_stop_silences),        cmocka_unit_test(test_roe_deferred_subfn),
        cmocka_unit_test(test_roe_timer_fires),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
