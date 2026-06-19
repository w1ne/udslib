/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"
#include "uds/uds_dtc.h"

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) subfn;
    (void) max_len;
    out_buf[0] = 0xAA;
    return 1;
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
    assert_false(ctx.p2_msg_pending);
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
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
