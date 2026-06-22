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

/* Unknown sub-function 0x08 -> NRC 0x12 (all of 0x00-0x07 are implemented). */
static void test_roe_unknown_subfn(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    uint8_t req[] = {0x86, 0x08, 0x02, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x12);
}

/* onComparisonOfValues (0x07): app reports an observed value via
 * uds_roe_trigger; the library emits when it satisfies the stored comparison. */
static void test_roe_compare_fires(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    /* 86 07 <window=02> <op=0x02 greater-than> <ref=0x00000064 (100)> <22 F1 90> */
    uint8_t setup[] = {0x86, 0x07, 0x02, 0x02, 0x00, 0x00, 0x00, 0x64, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* C6 07 <count=1> + echo(data[2..10], 9 bytes) = 12 */
    expect_value(mock_tp_send, len, 12);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 11);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* observed 150 > 100 -> emit. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    assert_int_equal(uds_roe_trigger(&ctx, 0x07, 150u), 1);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x07);

    /* observed 50 > 100 is false -> no emission. */
    assert_int_equal(uds_roe_trigger(&ctx, 0x07, 50u), 0);
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

/* A stored definition survives serialize -> (fresh ctx) deserialize: after
 * restore + start, a trigger emits exactly as the original would have. */
static void test_roe_serialize_roundtrip(void **state)
{
    (void) state;
    uint8_t blob[64];
    int blob_len;

    {
        BEGIN_UDS_TEST(ctx, cfg);
        cfg.did_table = k_did_table;
        uint8_t setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
        will_return(mock_get_time, 1000);
        will_return(mock_get_time, 1000);
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 9);
        will_return(mock_tp_send, 0);
        uds_input_sdu(&ctx, setup, 8);

        blob_len = uds_roe_serialize(&ctx, blob, sizeof(blob));
        assert_true(blob_len > 0);
    }

    /* Fresh context: no events until we restore. */
    BEGIN_UDS_TEST(ctx2, cfg2);
    cfg2.did_table = k_did_table;

    int restored = uds_roe_deserialize(&ctx2, blob, (uint16_t) blob_len);
    assert_int_equal(restored, 1);

    /* Start the restored event. */
    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx2, start, 2);

    /* Trigger -> emits the restored serviceToRespondTo (22 F1 90 -> 62 ...). */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    int emitted = uds_roe_trigger(&ctx2, 0x03, 0xF190u);
    assert_int_equal(emitted, 1);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[3], 0x62);
}

/* onComparisonOfValues less-than (op 0x03) and equal (op 0x01 default). */
static void test_roe_compare_less_than_and_equal(void **state)
{
    (void) state;
    {
        BEGIN_UDS_TEST(ctx, cfg);
        cfg.did_table = k_did_table;
        /* op 0x03 less-than, ref 100. */
        uint8_t setup[] = {0x86, 0x07, 0x02, 0x03, 0x00, 0x00, 0x00, 0x64, 0x22, 0xF1, 0x90};
        will_return(mock_get_time, 1000);
        will_return(mock_get_time, 1000);
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 12);
        will_return(mock_tp_send, 0);
        uds_input_sdu(&ctx, setup, 11);

        uint8_t start[] = {0x86, 0x05};
        will_return(mock_get_time, 1000);
        will_return(mock_get_time, 1000);
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 3);
        will_return(mock_tp_send, 0);
        uds_input_sdu(&ctx, start, 2);

        /* 50 < 100 -> emit; 150 < 100 false -> no emit. */
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 8);
        will_return(mock_tp_send, 0);
        assert_int_equal(uds_roe_trigger(&ctx, 0x07, 50u), 1);
        assert_int_equal(uds_roe_trigger(&ctx, 0x07, 150u), 0);
    }
    {
        BEGIN_UDS_TEST(ctx, cfg);
        cfg.did_table = k_did_table;
        /* op 0x01 equal (the default branch), ref 100. */
        uint8_t setup[] = {0x86, 0x07, 0x02, 0x01, 0x00, 0x00, 0x00, 0x64, 0x22, 0xF1, 0x90};
        will_return(mock_get_time, 1000);
        will_return(mock_get_time, 1000);
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 12);
        will_return(mock_tp_send, 0);
        uds_input_sdu(&ctx, setup, 11);

        uint8_t start[] = {0x86, 0x05};
        will_return(mock_get_time, 1000);
        will_return(mock_get_time, 1000);
        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 3);
        will_return(mock_tp_send, 0);
        uds_input_sdu(&ctx, start, 2);

        expect_any(mock_tp_send, data);
        expect_value(mock_tp_send, len, 8);
        will_return(mock_tp_send, 0);
        assert_int_equal(uds_roe_trigger(&ctx, 0x07, 100u), 1); /* equal -> emit */
        assert_int_equal(uds_roe_trigger(&ctx, 0x07, 101u), 0);
    }
}

/* reportActivatedEvents (0x04) lists each active event's type. */
static void test_roe_report_activated(void **state)
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

    /* 0x04 reportActivatedEvents: C6 04 <count=1> <event_type 0x03> = 4 bytes. */
    uint8_t report[] = {0x86, 0x04};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, report, 2);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(g_tx_buf[2], 0x01); /* one active */
    assert_int_equal(g_tx_buf[3], 0x03); /* event type 0x03 */
}

/* clearResponseOnEvent (0x06) wipes the table; a subsequent trigger is silent. */
static void test_roe_clear_wipes(void **state)
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

    uint8_t clear[] = {0x86, 0x06};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* C6 06 <count=0> */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, clear, 2);
    assert_int_equal(g_tx_buf[2], 0x00); /* count back to 0 */

    /* Trigger after clear: nothing stored, nothing emitted. */
    assert_int_equal(uds_roe_trigger(&ctx, 0x03, 0xF190u), 0);
}

/* A finite event window deactivates the slot once it expires in uds_process. */
static void test_roe_window_expiry_deactivates(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    /* onTimerInterrupt with a NON-infinite window (window byte 0x01). */
    uint8_t setup[] = {0x86, 0x02, 0x01, 0x03, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 7);

    /* start: window_deadline = now(1000) + UDS_ROE_WINDOW_MS. */
    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000); /* start path reads time (need_time true) */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* Advance well past the window: uds_process deactivates the slot. No emit. */
    will_return(mock_get_time, 1000u + UDS_ROE_WINDOW_MS + 1u);
    uds_process(&ctx);

    /* The slot is now inactive: a trigger is silent. */
    assert_int_equal(uds_roe_trigger(&ctx, 0x02, 0u), 0);
}

/* onTimerInterrupt at slow (default) and medium rates exercises roe_rate_to_ms. */
static void test_roe_timer_slow_rate(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    /* rate byte 0x01 -> slow (1000ms). */
    uint8_t setup[] = {0x86, 0x02, 0x02, 0x01, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 7);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* Prime next_fire = 1000 + 1000. */
    will_return(mock_get_time, 1000);
    uds_process(&ctx);

    /* Past the slow interval -> emit. */
    will_return(mock_get_time, 2100);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    assert_int_equal(g_tx_buf[1], 0x02);
}

/* onDTCStatusChange (0x01): a status-mask match triggers an emission
 * (uds_service_roe.c:265-266). */
static void test_roe_dtc_status_change_match(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    /* 86 01 <window=02 infinite> <DTCStatusMask=0x08> <serviceToRespondTo 22 F1 90> */
    uint8_t setup[] = {0x86, 0x01, 0x02, 0x08, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 7);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* Observed status 0x08 & mask 0x08 -> emit. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    assert_int_equal(uds_roe_trigger(&ctx, 0x01, 0x08u), 1);

    /* Observed status 0x04 & mask 0x08 == 0 -> no emit. */
    assert_int_equal(uds_roe_trigger(&ctx, 0x01, 0x04u), 0);
}

/* uds_roe_trigger on a NULL context returns NOT_INIT (uds_service_roe.c:236-237). */
static void test_roe_trigger_null(void **state)
{
    (void) state;
    assert_int_equal(uds_roe_trigger(NULL, 0x01, 0u), UDS_ERR_NOT_INIT);
}

/* roe_emit_slot with an inner dispatch that yields nothing must emit nothing
 * (uds_service_roe.c:182-183). The serviceToRespondTo is an RDBI for a DID with
 * no registered table, so the inner response is suppressed (functional gate) /
 * empty capture. */
static void test_roe_emit_empty_capture(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* No DID table at all: the inner 22 F1 90 produces an NRC capture which is
     * 3 bytes, so to force an EMPTY capture we instead store a serviceToRespondTo
     * whose inner dispatch is a suppressed-positive. Use TesterPresent with the
     * suppress bit set: handle_request clears the response, capture stays 0. */
    uint8_t setup[] = {0x86, 0x01, 0x02, 0x08, 0x3E, 0x80}; /* STR = 3E 80 (suppress) */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* C6 01 <count> + echo(4) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 6);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    /* Trigger: inner response is suppressed -> capture empty -> no 0xC6 emitted.
     * No tp_send expected: a strict-mock failure would fire if it emitted. */
    int emitted = uds_roe_trigger(&ctx, 0x01, 0x08u);
    assert_int_equal(emitted, 1); /* slot matched and ran, but emitted nothing */
}

/* Serialize argument guards and buffer-too-small (uds_service_roe.c:283-287). */
static void test_roe_serialize_guards(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t buf[64];
    assert_int_equal(uds_roe_serialize(NULL, buf, sizeof(buf)), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_roe_serialize(&ctx, NULL, sizeof(buf)), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_roe_serialize(&ctx, buf, 1u), UDS_ERR_BUFFER_TOO_SMALL);
}

/* Serialize into a buffer too small for a stored slot (uds_service_roe.c:298-299). */
static void test_roe_serialize_overflow(void **state)
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

    /* Header fits (2 bytes) but the slot body does not. */
    uint8_t small[3];
    assert_int_equal(uds_roe_serialize(&ctx, small, sizeof(small)), UDS_ERR_BUFFER_TOO_SMALL);
}

/* ROE capture overflow: when the inner service response is larger than the
 * roe_emit_slot capture buffer (UDS_ROE_STR_MAX + 8 = 16 bytes), the captured
 * dispatch now returns UDS_ERR_BUFFER_TOO_SMALL (<= 0), and roe_emit_slot must
 * NOT emit a truncated 0xC6 frame.  The test asserts silence by registering no
 * mock_tp_send expectation for the trigger call — cmocka will fail if send fires. */
static int mock_did_read_large(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    /* 14 data bytes -> inner response 62 F1 90 + 14 = 17 bytes > 16-byte cap buffer */
    uint16_t fill = (max_len < 14u) ? max_len : 14u;
    memset(buf, 0xAB, fill);
    return (int) fill;
}

static const uds_did_entry_t k_dids_large[] = {
    {0xF190u, 14u, 0u, 0u, mock_did_read_large, NULL, NULL},
};

static uds_did_table_t k_did_table_large = {k_dids_large, 1u};

static void test_roe_capture_overflow_drops_frame(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table_large;

    /* Setup onChangeOfDataIdentifier(0xF190) -> serviceToRespondTo = 22 F1 90. */
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

    /* Trigger: inner 22 F1 90 -> 62 F1 90 + 14 bytes = 17 bytes, overflows the
     * 16-byte capture buffer.  uds_internal_dispatch_captured returns
     * UDS_ERR_BUFFER_TOO_SMALL; roe_emit_slot must drop silently.
     * No mock_tp_send expectation -> test fails if any frame is transmitted. */
    int emitted = uds_roe_trigger(&ctx, 0x03, 0xF190u);
    /* The slot matched and ran (emitted counter incremented in uds_roe_trigger),
     * but no frame should have been put on the bus. */
    assert_int_equal(emitted, 1);
}

/* Deserialize argument / version / truncation guards
 * (uds_service_roe.c:320-324, :332-333, :343-345). */
static void test_roe_deserialize_guards(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t good[2] = {0x01, 0x00}; /* version 1, count 0 */

    assert_int_equal(uds_roe_deserialize(NULL, good, sizeof(good)), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_roe_deserialize(&ctx, NULL, sizeof(good)), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_roe_deserialize(&ctx, good, 1u), UDS_ERR_INVALID_ARG); /* len < 2 */

    uint8_t bad_ver[2] = {0x99, 0x00};
    assert_int_equal(uds_roe_deserialize(&ctx, bad_ver, sizeof(bad_ver)), UDS_ERR_INVALID_ARG);

    /* count=1 but the slot header is truncated. */
    uint8_t trunc_hdr[3] = {0x01, 0x01, 0x02};
    assert_int_equal(uds_roe_deserialize(&ctx, trunc_hdr, sizeof(trunc_hdr)), UDS_ERR_INVALID_ARG);

    /* count=1, full 7-byte header but str_len=0 (invalid). */
    uint8_t bad_strlen[9] = {0x01, 0x01, 0x01, 0, 0, 0, 0x08, 0x02, 0x00};
    assert_int_equal(uds_roe_deserialize(&ctx, bad_strlen, sizeof(bad_strlen)),
                     UDS_ERR_INVALID_ARG);
}

/* onTimerInterrupt at medium rate (0x02) exercises roe_rate_to_ms (:200-201). */
static void test_roe_timer_medium_rate(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table = k_did_table;

    uint8_t setup[] = {0x86, 0x02, 0x02, 0x02, 0x22, 0xF1, 0x90}; /* rate byte 0x02 */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, 7);

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, 2);

    will_return(mock_get_time, 1000); /* prime next_fire = 1000 + 500 */
    uds_process(&ctx);

    will_return(mock_get_time, 1600); /* past medium interval -> emit */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    uds_process(&ctx);
    assert_int_equal(g_tx_buf[1], 0x02);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_roe_setup_and_start_acks),
        cmocka_unit_test(test_roe_timer_medium_rate),
        cmocka_unit_test(test_roe_trigger_emits),
        cmocka_unit_test(test_roe_stop_silences),
        cmocka_unit_test(test_roe_unknown_subfn),
        cmocka_unit_test(test_roe_timer_fires),
        cmocka_unit_test(test_roe_serialize_roundtrip),
        cmocka_unit_test(test_roe_compare_fires),
        cmocka_unit_test(test_roe_compare_less_than_and_equal),
        cmocka_unit_test(test_roe_report_activated),
        cmocka_unit_test(test_roe_clear_wipes),
        cmocka_unit_test(test_roe_window_expiry_deactivates),
        cmocka_unit_test(test_roe_timer_slow_rate),
        cmocka_unit_test(test_roe_dtc_status_change_match),
        cmocka_unit_test(test_roe_trigger_null),
        cmocka_unit_test(test_roe_emit_empty_capture),
        cmocka_unit_test(test_roe_capture_overflow_drops_frame),
        cmocka_unit_test(test_roe_serialize_guards),
        cmocka_unit_test(test_roe_serialize_overflow),
        cmocka_unit_test(test_roe_deserialize_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
