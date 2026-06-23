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

/* §2c: ROE setup echo body that would overflow a deliberately small tx_buffer
 * must yield NRC 0x14 (responseTooLong) and must not write past the buffer. */
static void test_roe_setup_echo_overflow_nrc(void **state)
{
    (void) state;

    /* tx_buffer is 8 bytes.  The positive response needs 3 + 6 = 9 bytes.
     * Place a canary byte immediately after the 8-byte region: if a pre-guard
     * memcpy fires before the post-hoc check, the canary will be overwritten. */
    static struct {
        uint8_t buf[8];
        uint8_t canary;
    } region;
    static uint8_t rx_buf[64];

    memset(&region, 0xBB, sizeof(region)); /* initialise including canary */

    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = region.buf;
    cfg.tx_buffer_size = sizeof(region.buf); /* 8 bytes — too small for 9-byte response */
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.did_table = k_did_table;
    uds_init(&ctx, &cfg);

    /* 86 03 <window=0x02> <DID=F1 90> <22 F1 90> — echo body = data[2..7] = 6 bytes.
     * Positive response = 3 + 6 = 9 bytes which exceeds the 8-byte buffer. */
    uint8_t setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* Expect NRC: 7F 86 14 (3 bytes) */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, setup, sizeof(setup));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x86);
    assert_int_equal(g_tx_buf[2], 0x14); /* NRC_RESPONSE_TOO_LONG */
    /* Canary must be intact: no byte was written past tx_buffer[7]. */
    assert_int_equal(region.canary, 0xBB);
}

/* §2: roe_emit_slot must not copy into tx_buffer when 3+cap would exceed
 * tx_buffer_size.  Strategy: set up and start with a normal-sized buffer, then
 * shrink tx_buffer_size to 6 before the trigger.  The inner DID read response
 * is 5 bytes (62 F1 90 DE AD), so the ROE emit frame would need 3+5=8 bytes —
 * exceeding the shrunken limit of 6.  A canary byte at g_tx_buf[6] must remain
 * intact, and no tp_send must be called. */
static void test_roe_emit_overflow_skipped(void **state)
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
    uds_input_sdu(&ctx, setup, sizeof(setup));

    uint8_t start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, start, sizeof(start));

    /* Shrink the tx window so 3+5=8 byte emit frame no longer fits.
     * Place a canary at the new limit to catch any out-of-bounds write. */
    cfg.tx_buffer_size = 6u;
    uint8_t canary_val = 0xCC;
    g_tx_buf[6] = canary_val;

    /* Trigger: emit would need 8 bytes but tx_buffer_size=6 -> skip.
     * No tp_send call expected (cmocka fails if it is called).
     * The slot matched so emitted=1, but no frame was sent. */
    int emitted = uds_roe_trigger(&ctx, 0x03, 0xF190u);
    assert_int_equal(emitted, 1); /* slot matched */
    assert_int_equal(g_tx_buf[6], canary_val); /* no overflow past byte 6 */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_roe_setup_and_start_acks),
        cmocka_unit_test(test_roe_trigger_emits),
        cmocka_unit_test(test_roe_stop_silences),
        cmocka_unit_test(test_roe_unknown_subfn),
        cmocka_unit_test(test_roe_timer_fires),
        cmocka_unit_test(test_roe_serialize_roundtrip),
        cmocka_unit_test(test_roe_compare_fires),
        cmocka_unit_test(test_roe_setup_echo_overflow_nrc),
        cmocka_unit_test(test_roe_emit_overflow_skipped),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
