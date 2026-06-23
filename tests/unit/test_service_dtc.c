/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req, uint16_t req_len,
                         uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) subfn;
    (void) req;
    (void) req_len;
    (void) max_len;
    out_buf[0] = 0xAA;
    return 1;
}

static uint8_t g_seen_req[8];
static uint16_t g_seen_req_len;
static uint8_t g_seen_sub;

static int capture_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req,
                            uint16_t req_len, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    g_seen_sub = subfn;
    g_seen_req_len = req_len;
    for (uint16_t i = 0u; (i < req_len) && (i < sizeof(g_seen_req)); i++) {
        g_seen_req[i] = req[i];
    }
    out_buf[0] = 0xAA;
    return 1;
}

static void test_read_dtc_info_fn_dtc_read_gets_request(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = capture_dtc_read; /* no fn_dtc_list: 0x0F routes to raw hook */
    g_seen_req_len = 0u;
    g_seen_sub = 0u;

    /* 0x0F mirror-memory-by-status-mask carries a status mask byte. */
    uint8_t req[] = {0x19, 0x0F, 0xA5};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 59 0F AA */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_sub, 0x0Fu);
    assert_int_equal(g_seen_req_len, 3);
    assert_int_equal(g_seen_req[0], 0x19); /* SID */
    assert_int_equal(g_seen_req[1], 0x0F); /* sub-function */
    assert_int_equal(g_seen_req[2], 0xA5); /* status mask reached the app */
}

static const uds_dtc_record_t k_dtcs[] = {
    {0x123456u, 0x09u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u, 0, 0, 0},
    {0x123457u, 0x08u, UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT, 0x11u, 0, 0, 0},
    {0xABCDEFu, 0x01u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0x12u, 0, 0, 0},
};

static int mock_dtc_list(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                         uint16_t max)
{
    (void) ctx;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < 3u; i++) {
        bool match = (status_mask == 0u) || ((k_dtcs[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = k_dtcs[i];
            }
            n++;
        }
    }
    return (int) n;
}

static void test_read_dtc_info_0x01_number_by_status(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;

    /* 0x19 0x01 <statusMask=0xFF>: all 3 DTCs match -> count 3. */
    uint8_t req[] = {0x19, 0x01, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 01 <avail> <format> <countHi> <countLo> = 6 bytes */
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x01);
    assert_int_equal(g_tx_buf[2], 0x7F); /* status availability mask */
    assert_int_equal(g_tx_buf[3], 0x01); /* DTC format identifier */
    assert_int_equal(g_tx_buf[4], 0x00); /* count high */
    assert_int_equal(g_tx_buf[5], 0x03); /* count low */
}

static void test_read_dtc_info_0x02_by_status(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x19 0x02 <statusMask=0x08>: 0x123456 and 0x123457 match (status & 0x08). */
    uint8_t req[] = {0x19, 0x02, 0x08};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 02 <avail> + 2 * (DTC[3] + status[1]) = 3 + 8 = 11 */
    expect_value(mock_tp_send, len, 11);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_tx_buf[2], 0x7F);
    /* First record: 12 34 56 09 */
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[4], 0x34);
    assert_int_equal(g_tx_buf[5], 0x56);
    assert_int_equal(g_tx_buf[6], 0x09);
    /* Second record: 12 34 57 08 */
    assert_int_equal(g_tx_buf[7], 0x12);
    assert_int_equal(g_tx_buf[8], 0x34);
    assert_int_equal(g_tx_buf[9], 0x57);
    assert_int_equal(g_tx_buf[10], 0x08);
}

static void test_read_dtc_info_0x0A_supported(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x19 0x0A reportSupportedDTC: no mask byte; all 3 DTCs returned. */
    uint8_t req[] = {0x19, 0x0A};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 0A <avail> + 3 * 4 = 3 + 12 = 15 */
    expect_value(mock_tp_send, len, 15);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x0A);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x12); /* first DTC */
    assert_int_equal(g_tx_buf[6], 0x09);
    assert_int_equal(g_tx_buf[11], 0xAB); /* third DTC */
    assert_int_equal(g_tx_buf[14], 0x01);
}

static int mock_dtc_snapshot(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    assert_int_equal(dtc, 0x123456u);
    assert_int_equal(record_num, 0x01u);
    out_buf[0] = 0x09; /* statusOfDTC */
    out_buf[1] = 0x01; /* snapshot record number */
    out_buf[2] = 0x00; /* number of identifiers */
    return 3;
}

static int mock_dtc_extdata(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num, uint8_t *out_buf,
                            uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    assert_int_equal(dtc, 0x123456u);
    assert_int_equal(record_num, 0x05u);
    out_buf[0] = 0x09; /* statusOfDTC */
    out_buf[1] = 0x05; /* ext data record number */
    out_buf[2] = 0xAB; /* data */
    return 3;
}

static void test_read_dtc_info_0x04_snapshot(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_snapshot = mock_dtc_snapshot;

    /* 0x19 0x04 <DTC=12 34 56> <recordNum=01> */
    uint8_t req[] = {0x19, 0x04, 0x12, 0x34, 0x56, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 04 12 34 56 + [09 01 00] = 5 + 3 = 8 */
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 6);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(g_tx_buf[2], 0x12); /* echoed DTC */
    assert_int_equal(g_tx_buf[3], 0x34);
    assert_int_equal(g_tx_buf[4], 0x56);
    assert_int_equal(g_tx_buf[5], 0x09); /* app payload */
}

static void test_read_dtc_info_0x04_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_snapshot = mock_dtc_snapshot;

    /* 0x04 requires DTC(3) + recordNumber(1): len 6. Here len 5 -> NRC 0x13. */
    uint8_t req[] = {0x19, 0x04, 0x12, 0x34, 0x56};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_read_dtc_info_0x06_extdata(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_extdata = mock_dtc_extdata;

    /* 0x19 0x06 <DTC=12 34 56> <recordNum=05> */
    uint8_t req[] = {0x19, 0x06, 0x12, 0x34, 0x56, 0x05};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 6);

    assert_int_equal(g_tx_buf[1], 0x06);
    assert_int_equal(g_tx_buf[4], 0x56);
    assert_int_equal(g_tx_buf[7], 0xAB);
}

static void test_read_dtc_info_response_too_long(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.tx_buffer_size = 6; /* room for 0 records -> all matches overflow */

    uint8_t req[] = {0x19, 0x02, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 19 14 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[2], 0x14); /* ResponseTooLong */
}

static void test_read_dtc_info_legacy_fallback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read; /* no fn_dtc_list -> legacy path */

    uint8_t req[] = {0x19, 0x02, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 02 <sub-echo> + legacy 1 byte (0xAA) = 3 */
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[2], 0xAA); /* legacy payload, not structured */
}

static void test_read_dtc_info_mask_missing(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read;

    /* C-10: Subfunction 0x01 requires a status mask. Total len 3. If len 2 -> NRC 0x13 */
    uint8_t req[] = {0x19, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 19 13 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_control_dtc_setting_suppress(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* C-16: 0x85 0x81 (Suppress response) */
    uint8_t req[] = {0x85, 0x81};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No tp_send expected for positive response */

    uds_input_sdu(&ctx, req, 2);
    assert_false(ctx.server.p2_msg_pending);
}

static void test_read_dtc_info_0x08_by_severity(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* sevMask=0x80 (checkImmediately) matches only 0x123456; statMask=0xFF. */
    uint8_t req[] = {0x19, 0x08, 0x80, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 08 <avail> + 1 * (sev funcUnit DTC[3] status) = 3 + 6 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x08);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x80); /* severity */
    assert_int_equal(g_tx_buf[4], 0x10); /* functional unit */
    assert_int_equal(g_tx_buf[5], 0x12); /* DTC hi */
    assert_int_equal(g_tx_buf[6], 0x34);
    assert_int_equal(g_tx_buf[7], 0x56);
    assert_int_equal(g_tx_buf[8], 0x09); /* status */
}

static void test_read_dtc_info_0x07_number_by_severity(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;

    /* sevMask=0xE0 matches all three; statMask=0xFF. */
    uint8_t req[] = {0x19, 0x07, 0xE0, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);

    assert_int_equal(g_tx_buf[1], 0x07);
    assert_int_equal(g_tx_buf[3], 0x01); /* format */
    assert_int_equal(g_tx_buf[4], 0x00); /* count hi */
    assert_int_equal(g_tx_buf[5], 0x03); /* count lo */
}

static void test_read_dtc_info_0x09_severity_info(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x09 <DTC=12 34 57> -> single record for 0x123457. */
    uint8_t req[] = {0x19, 0x09, 0x12, 0x34, 0x57};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    assert_int_equal(g_tx_buf[1], 0x09);
    assert_int_equal(g_tx_buf[2], 0x7F); /* status avail */
    assert_int_equal(g_tx_buf[3], 0x40); /* severity (checkAtNextHalt) */
    assert_int_equal(g_tx_buf[4], 0x11); /* functional unit */
    assert_int_equal(g_tx_buf[5], 0x12); /* DTC hi */
    assert_int_equal(g_tx_buf[6], 0x34); /* DTC mid */
    assert_int_equal(g_tx_buf[7], 0x57); /* DTC lo */
    assert_int_equal(g_tx_buf[8], 0x08); /* status */
}

static const uds_dtc_record_t k_fdc_dtcs[] = {
    {0x111111u, 0x04u, 0, 0, 0x20, 0, 0}, /* FDC 32 -> in progress, reported */
    {0x222222u, 0x08u, 0, 0, 0x7F, 0, 0}, /* FDC 127 -> confirmed, not reported */
    {0x333333u, 0x00u, 0, 0, 0x00, 0, 0}, /* FDC 0 -> not reported */
};

static int mock_dtc_list_fdc(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    for (uint16_t i = 0u; i < 3u; i++) {
        if ((out != NULL) && (i < max)) {
            out[i] = k_fdc_dtcs[i];
        }
    }
    return 3;
}

static void test_read_dtc_info_0x14_fault_counter(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_fdc;

    uint8_t req[] = {0x19, 0x14};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 14 + 1 * (DTC[3] FDC[1]) = 2 + 4 = 6 (only 0x111111 in range) */
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x14);
    assert_int_equal(g_tx_buf[2], 0x11); /* DTC hi */
    assert_int_equal(g_tx_buf[3], 0x11);
    assert_int_equal(g_tx_buf[4], 0x11);
    assert_int_equal(g_tx_buf[5], 0x20); /* FDC = 32 */
}

static const uds_dtc_record_t k_wwh_dtcs[] = {
    /* dtc, status, severity, funcUnit, fdc, aging, functional_group */
    {0xA00001u, 0x08u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0, 0, 0, UDS_DTC_FGID_EMISSIONS},
    {0xA00002u, 0x01u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0, 0, 0, UDS_DTC_FGID_SAFETY},
};

static int mock_dtc_list_wwh(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < 2u; i++) {
        bool match = (status_mask == 0u) || ((k_wwh_dtcs[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = k_wwh_dtcs[i];
            }
            n++;
        }
    }
    return (int) n;
}

static void test_read_dtc_info_0x42_wwhobd(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_wwh;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_severity_availability_mask = 0xE0u;
    cfg.dtc_format_id = 0x04u;

    /* FGID=0x33 emissions, statMask=0xFF, sevMask=0xFF -> only 0xA00001. */
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 42 FGID statAvail sevAvail format + 1*(sev DTC[3] status) = 6 + 5 = 11 */
    expect_value(mock_tp_send, len, 11);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    assert_int_equal(g_tx_buf[1], 0x42);
    assert_int_equal(g_tx_buf[2], 0x33);  /* FGID echoed */
    assert_int_equal(g_tx_buf[3], 0x7F);  /* status avail */
    assert_int_equal(g_tx_buf[4], 0xE0);  /* severity avail */
    assert_int_equal(g_tx_buf[5], 0x04);  /* format */
    assert_int_equal(g_tx_buf[6], 0x80);  /* severity */
    assert_int_equal(g_tx_buf[7], 0xA0);  /* DTC hi */
    assert_int_equal(g_tx_buf[8], 0x00);  /* DTC mid */
    assert_int_equal(g_tx_buf[9], 0x01);  /* DTC lo */
    assert_int_equal(g_tx_buf[10], 0x08); /* status */
}

static void test_read_dtc_info_0x55_permanent(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_wwh;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x04u;

    /* FGID=0x33; 0xA00001 is confirmed (status 0x08) -> permanent. */
    uint8_t req[] = {0x19, 0x55, 0x33};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 55 FGID statAvail format + 1*(DTC[3] status) = 5 + 4 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[1], 0x55);
    assert_int_equal(g_tx_buf[2], 0x33); /* FGID */
    assert_int_equal(g_tx_buf[3], 0x7F); /* status avail */
    assert_int_equal(g_tx_buf[4], 0x04); /* format */
    assert_int_equal(g_tx_buf[5], 0xA0); /* DTC hi */
    assert_int_equal(g_tx_buf[6], 0x00); /* DTC mid */
    assert_int_equal(g_tx_buf[7], 0x01); /* DTC lo */
    assert_int_equal(g_tx_buf[8], 0x08); /* status */
}

static void test_read_dtc_info_0x0B_first_test_failed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    uint8_t req[] = {0x19, 0x0B};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* 59 0B 7F + 12 34 56 09 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x0B);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[4], 0x34);
    assert_int_equal(g_tx_buf[5], 0x56);
    assert_int_equal(g_tx_buf[6], 0x09);
}

static void test_read_dtc_info_0x0C_first_confirmed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    uint8_t req[] = {0x19, 0x0C};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* first confirmed = 0x123456 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[1], 0x0C);
    assert_int_equal(g_tx_buf[5], 0x56);
    assert_int_equal(g_tx_buf[6], 0x09);
}

static void test_read_dtc_info_0x0D_most_recent_test_failed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    uint8_t req[] = {0x19, 0x0D};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* last testFailed = 0xABCDEF / 0x01 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[1], 0x0D);
    assert_int_equal(g_tx_buf[3], 0xAB);
    assert_int_equal(g_tx_buf[4], 0xCD);
    assert_int_equal(g_tx_buf[5], 0xEF);
    assert_int_equal(g_tx_buf[6], 0x01);
}

static void test_read_dtc_info_0x0E_most_recent_confirmed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    uint8_t req[] = {0x19, 0x0E};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* last confirmed = 0x123457 / 0x08 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[1], 0x0E);
    assert_int_equal(g_tx_buf[5], 0x57);
    assert_int_equal(g_tx_buf[6], 0x08);
}

static void test_read_dtc_info_0x15_permanent(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    uint8_t req[] = {0x19, 0x15};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 15 7F + 2 confirmed (0x123456/09, 0x123457/08) = 3 + 8 = 11 */
    expect_value(mock_tp_send, len, 11);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[1], 0x15);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[6], 0x09);
    assert_int_equal(g_tx_buf[7], 0x12);
    assert_int_equal(g_tx_buf[10], 0x08);
}

static void test_read_dtc_info_0x0F_reaches_legacy(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read; /* no fn_dtc_list: niche sub now reachable via legacy hook */
    uint8_t req[] = {0x19, 0x0F, 0xFF}; /* 0x0F mirror-memory requires a status mask */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 59 0F AA -- proves it is NOT subFunctionNotSupported */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x0F);
    assert_int_equal(g_tx_buf[2], 0xAA);
}

static void test_store_backed_read_dtc_0x02(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;
    uds_dtc_store_init(&store, backing, 4u, 40u);
    uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                           UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_report_test(&store, 0x012345u, true); /* sets testFailed (0x01) */

    cfg.app_data = &store;
    cfg.fn_dtc_list = uds_dtc_store_list_cb;
    cfg.fn_dtc_clear = uds_dtc_store_clear_cb;
    cfg.dtc_status_availability_mask = 0x7Fu;

    uint8_t req[] = {0x19, 0x02, 0x01}; /* status mask testFailed */

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 02 <avail> + 1 * (DTC[3] status[1]) = 3 + 4 = 7 */
    expect_value(mock_tp_send, len, 7);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_tx_buf[3], 0x01); /* DTC hi */
    assert_int_equal(g_tx_buf[4], 0x23);
    assert_int_equal(g_tx_buf[5], 0x45);
    assert_true((g_tx_buf[6] & UDS_DTC_STATUS_TEST_FAILED) != 0u);
}

static void test_store_backed_extdata_and_clear(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;
    uds_dtc_store_init(&store, backing, 4u, 40u);
    uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                           UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_report_test(&store, 0x012345u, true);

    cfg.app_data = &store;
    cfg.fn_dtc_list = uds_dtc_store_list_cb;
    cfg.fn_dtc_extdata = uds_dtc_store_extdata_cb;
    cfg.fn_dtc_clear = uds_dtc_store_clear_cb;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x19 0x06 DTC=01 23 45, record_num=0x01 */
    uint8_t req_ext[] = {0x19, 0x06, 0x01, 0x23, 0x45, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 06 DTC(3) + extdata(4) = 5 + 4 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req_ext, 6);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x06);
    assert_int_equal(g_tx_buf[2], 0x01); /* DTC hi */
    assert_int_equal(g_tx_buf[3], 0x23); /* DTC mid */
    assert_int_equal(g_tx_buf[4], 0x45); /* DTC lo */
    assert_int_equal(g_tx_buf[6], 0x01); /* record_num echoed in payload[1] */

    /* ClearDiagnosticInformation: group 0xFFFFFF */
    uint8_t req_clr[] = {0x14, 0xFF, 0xFF, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req_clr, 4);

    assert_int_equal(g_tx_buf[0], 0x54);

    uds_dtc_record_t *r = uds_dtc_store_get(&store, 0x012345u);
    assert_non_null(r);
    assert_int_equal(r->status, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_dtc_info_0x01_number_by_status),
        cmocka_unit_test(test_read_dtc_info_0x02_by_status),
        cmocka_unit_test(test_read_dtc_info_0x0A_supported),
        cmocka_unit_test(test_read_dtc_info_0x04_snapshot),
        cmocka_unit_test(test_read_dtc_info_0x04_short),
        cmocka_unit_test(test_read_dtc_info_0x06_extdata),
        cmocka_unit_test(test_read_dtc_info_response_too_long),
        cmocka_unit_test(test_read_dtc_info_legacy_fallback),
        cmocka_unit_test(test_read_dtc_info_mask_missing),
        cmocka_unit_test(test_control_dtc_setting_suppress),
        cmocka_unit_test(test_read_dtc_info_0x08_by_severity),
        cmocka_unit_test(test_read_dtc_info_0x07_number_by_severity),
        cmocka_unit_test(test_read_dtc_info_0x09_severity_info),
        cmocka_unit_test(test_read_dtc_info_0x14_fault_counter),
        cmocka_unit_test(test_read_dtc_info_0x42_wwhobd),
        cmocka_unit_test(test_read_dtc_info_0x55_permanent),
        cmocka_unit_test(test_read_dtc_info_0x0B_first_test_failed),
        cmocka_unit_test(test_read_dtc_info_0x0C_first_confirmed),
        cmocka_unit_test(test_read_dtc_info_0x0D_most_recent_test_failed),
        cmocka_unit_test(test_read_dtc_info_0x0E_most_recent_confirmed),
        cmocka_unit_test(test_read_dtc_info_0x15_permanent),
        cmocka_unit_test(test_read_dtc_info_0x0F_reaches_legacy),
        cmocka_unit_test(test_store_backed_read_dtc_0x02),
        cmocka_unit_test(test_store_backed_extdata_and_clear),
        cmocka_unit_test(test_read_dtc_info_fn_dtc_read_gets_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
