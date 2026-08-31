/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_dtc_memory.c
 * @brief ReadDTCInformation (0x19) sub-functions the library formats from the
 *        record-number / snapshot-identification / memory-region hooks:
 *        0x03, 0x05, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x16, 0x17, 0x18, 0x19.
 */

#include "test_helpers.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

static const uds_dtc_record_t k_dtcs[] = {
    {0x123456u, 0x09u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u, 0, 0, UDS_DTC_FGID_EMISSIONS},
    {0x123457u, 0x08u, UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT, 0x11u, 0, 0, UDS_DTC_FGID_SAFETY},
    {0xABCDEFu, 0x01u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0x12u, 0, 0, UDS_DTC_FGID_EMISSIONS},
};

static int mock_dtc_list(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                         uint16_t max)
{
    (void) ctx;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < 3u; i++) {
        if ((status_mask == 0u) || ((k_dtcs[i].status & status_mask) != 0u)) {
            if ((out != NULL) && (n < max)) {
                out[n] = k_dtcs[i];
            }
            n++;
        }
    }
    return (int) n;
}

/* --- fn_dtc_list_mem (0x0F/0x11/0x12/0x13/0x17) --- */

static uds_dtc_memory_t g_seen_memory;
static uint8_t g_seen_mem_selection;
static uint8_t g_seen_status_mask;

static int mock_dtc_list_mem(struct uds_ctx *ctx, uds_dtc_memory_t memory, uint8_t mem_selection,
                             uint8_t status_mask, uds_dtc_record_t *out, uint16_t max)
{
    (void) ctx;
    g_seen_memory = memory;
    g_seen_mem_selection = mem_selection;
    g_seen_status_mask = status_mask;

    uint16_t n = 0u;
    for (uint16_t i = 0u; i < 3u; i++) {
        /* Emissions-related OBD reports functional group 0x33 only; mirror and
         * user-defined memory report the whole set. */
        if ((memory == UDS_DTC_MEM_EMISSIONS_OBD) &&
            (k_dtcs[i].functional_group != UDS_DTC_FGID_EMISSIONS)) {
            continue;
        }
        if ((status_mask == 0u) || ((k_dtcs[i].status & status_mask) != 0u)) {
            if ((out != NULL) && (n < max)) {
                out[n] = k_dtcs[i];
            }
            n++;
        }
    }
    return (int) n;
}

static void test_read_dtc_info_0x0F_mirror_by_status(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x19 0x0F <statusMask=0x08>: 0x123456 and 0x123457 match. */
    uint8_t req[] = {0x19, 0x0F, 0x08};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 11); /* 59 0F <avail> + 2 * 4 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_MIRROR);
    assert_int_equal(g_seen_mem_selection, 0x00);
    assert_int_equal(g_seen_status_mask, 0x08);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x0F);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[4], 0x34);
    assert_int_equal(g_tx_buf[5], 0x56);
    assert_int_equal(g_tx_buf[6], 0x09);
    assert_int_equal(g_tx_buf[9], 0x57);
    assert_int_equal(g_tx_buf[10], 0x08);
}

static void test_read_dtc_info_0x11_number_of_mirror(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;

    /* 0x19 0x11 <statusMask=0xFF>: all 3 mirror DTCs counted. */
    uint8_t req[] = {0x19, 0x11, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 59 11 <avail> <format> <countHi> <countLo> */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_MIRROR);
    assert_int_equal(g_tx_buf[1], 0x11);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x01);
    assert_int_equal(g_tx_buf[4], 0x00);
    assert_int_equal(g_tx_buf[5], 0x03);
}

static void test_read_dtc_info_0x12_number_of_emissions(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;

    /* Only the two functional-group 0x33 DTCs are emissions-related. */
    uint8_t req[] = {0x19, 0x12, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_EMISSIONS_OBD);
    assert_int_equal(g_tx_buf[1], 0x12);
    assert_int_equal(g_tx_buf[5], 0x02);
}

static void test_read_dtc_info_0x13_emissions_by_status(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;
    cfg.dtc_status_availability_mask = 0x7Fu;

    uint8_t req[] = {0x19, 0x13, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 11); /* 59 13 <avail> + 2 * 4 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_EMISSIONS_OBD);
    assert_int_equal(g_tx_buf[1], 0x13);
    assert_int_equal(g_tx_buf[3], 0x12); /* 0x123456 */
    assert_int_equal(g_tx_buf[7], 0xAB); /* 0xABCDEF */
    assert_int_equal(g_tx_buf[10], 0x01);
}

static void test_read_dtc_info_0x17_user_def_by_status(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x19 0x17 <statusMask=0x08> <memorySelection=0x42>. */
    uint8_t req[] = {0x19, 0x17, 0x08, 0x42};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 12); /* 59 17 <memSel> <avail> + 2 * 4 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_USER_DEFINED);
    assert_int_equal(g_seen_mem_selection, 0x42);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x17);
    assert_int_equal(g_tx_buf[2], 0x42); /* MemorySelection echoed */
    assert_int_equal(g_tx_buf[3], 0x7F); /* status availability mask */
    assert_int_equal(g_tx_buf[4], 0x12);
    assert_int_equal(g_tx_buf[7], 0x09);
}

static void test_read_dtc_info_0x17_missing_memory_selection(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;

    /* 0x17 needs statusMask + MemorySelection: len 4. Here len 3 -> NRC 0x13. */
    uint8_t req[] = {0x19, 0x17, 0x08};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_read_dtc_info_0x0F_suppress_positive(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list_mem = mock_dtc_list_mem;

    /* suppressPosRspMsgIndicationBit set -> no response emitted. */
    uint8_t req[] = {0x19, 0x8F, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&ctx, req, 3);
    assert_int_equal(g_seen_memory, UDS_DTC_MEM_MIRROR);
}

/* --- fn_dtc_extdata_mem (0x10/0x19) and fn_dtc_snapshot_mem (0x18) --- */

static int mock_dtc_extdata_mem(struct uds_ctx *ctx, uds_dtc_memory_t memory, uint8_t mem_selection,
                                uint32_t dtc, uint8_t record_num, uint8_t *out_buf,
                                uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    g_seen_memory = memory;
    g_seen_mem_selection = mem_selection;
    assert_int_equal(dtc, 0x123456u);
    out_buf[0] = 0x09;       /* statusOfDTC */
    out_buf[1] = record_num; /* extended data record number */
    out_buf[2] = 0x7Eu;      /* record payload */
    return 3;
}

static int mock_dtc_snapshot_mem(struct uds_ctx *ctx, uint8_t mem_selection, uint32_t dtc,
                                 uint8_t record_num, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    g_seen_mem_selection = mem_selection;
    assert_int_equal(dtc, 0x123456u);
    out_buf[0] = 0x09;       /* statusOfDTC */
    out_buf[1] = record_num; /* snapshot record number */
    out_buf[2] = 0x00;       /* number of identifiers */
    return 3;
}

static void test_read_dtc_info_0x10_mirror_extdata(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_extdata_mem = mock_dtc_extdata_mem;

    /* 0x19 0x10 <DTC=123456> <recordNumber=0x05>. */
    uint8_t req[] = {0x19, 0x10, 0x12, 0x34, 0x56, 0x05};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8); /* 59 10 DTC(3) + 3 payload */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 6);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_MIRROR);
    assert_int_equal(g_seen_mem_selection, 0x00);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x10);
    assert_int_equal(g_tx_buf[2], 0x12);
    assert_int_equal(g_tx_buf[3], 0x34);
    assert_int_equal(g_tx_buf[4], 0x56);
    assert_int_equal(g_tx_buf[5], 0x09); /* statusOfDTC */
    assert_int_equal(g_tx_buf[6], 0x05); /* record number */
    assert_int_equal(g_tx_buf[7], 0x7E);
}

static void test_read_dtc_info_0x19_user_def_extdata(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_extdata_mem = mock_dtc_extdata_mem;

    /* 0x19 0x19 <DTC=123456> <recordNumber=0x02> <memorySelection=0x42>. */
    uint8_t req[] = {0x19, 0x19, 0x12, 0x34, 0x56, 0x02, 0x42};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9); /* 59 19 <memSel> DTC(3) + 3 payload */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 7);

    assert_int_equal(g_seen_memory, UDS_DTC_MEM_USER_DEFINED);
    assert_int_equal(g_seen_mem_selection, 0x42);
    assert_int_equal(g_tx_buf[1], 0x19);
    assert_int_equal(g_tx_buf[2], 0x42); /* MemorySelection echoed */
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[4], 0x34);
    assert_int_equal(g_tx_buf[5], 0x56);
    assert_int_equal(g_tx_buf[6], 0x09);
    assert_int_equal(g_tx_buf[7], 0x02);
}

static void test_read_dtc_info_0x18_user_def_snapshot(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_snapshot_mem = mock_dtc_snapshot_mem;

    uint8_t req[] = {0x19, 0x18, 0x12, 0x34, 0x56, 0x01, 0x33};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 7);

    assert_int_equal(g_seen_mem_selection, 0x33);
    assert_int_equal(g_tx_buf[1], 0x18);
    assert_int_equal(g_tx_buf[2], 0x33);
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[6], 0x09); /* statusOfDTC */
    assert_int_equal(g_tx_buf[7], 0x01); /* snapshot record number */
}

static void test_read_dtc_info_0x18_missing_memory_selection(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_snapshot_mem = mock_dtc_snapshot_mem;

    /* 0x18 needs DTC(3) + recordNumber + MemorySelection: len 7. Here 6 -> NRC 0x13. */
    uint8_t req[] = {0x19, 0x18, 0x12, 0x34, 0x56, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 6);
    assert_int_equal(g_tx_buf[2], 0x13);
}

/* --- fn_dtc_snapshot_ids (0x03) --- */

static int mock_dtc_snapshot_ids(struct uds_ctx *ctx, uint32_t dtc, uint8_t *out_records,
                                 uint16_t max)
{
    (void) ctx;
    (void) max;
    if (dtc == 0x123456u) {
        out_records[0] = 0x01;
        out_records[1] = 0x02;
        return 2;
    }
    if (dtc == 0xABCDEFu) {
        out_records[0] = 0x01;
        return 1;
    }
    return 0; /* 0x123457 has no stored snapshot */
}

static void test_read_dtc_info_0x03_snapshot_identification(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.fn_dtc_snapshot_ids = mock_dtc_snapshot_ids;

    uint8_t req[] = {0x19, 0x03};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 14); /* 59 03 + 3 * (DTC[3] + recNum[1]) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x03);
    /* 0x123456 record 0x01 */
    assert_int_equal(g_tx_buf[2], 0x12);
    assert_int_equal(g_tx_buf[3], 0x34);
    assert_int_equal(g_tx_buf[4], 0x56);
    assert_int_equal(g_tx_buf[5], 0x01);
    /* 0x123456 record 0x02 */
    assert_int_equal(g_tx_buf[6], 0x12);
    assert_int_equal(g_tx_buf[9], 0x02);
    /* 0xABCDEF record 0x01 (0x123457 contributes nothing) */
    assert_int_equal(g_tx_buf[10], 0xAB);
    assert_int_equal(g_tx_buf[11], 0xCD);
    assert_int_equal(g_tx_buf[12], 0xEF);
    assert_int_equal(g_tx_buf[13], 0x01);
}

/* --- fn_dtc_stored_data (0x05) and fn_dtc_extdata_by_record (0x16) --- */

static uint8_t g_seen_record_num;

static int mock_dtc_stored_data(struct uds_ctx *ctx, uint8_t record_num, uint8_t *out_buf,
                                uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    g_seen_record_num = record_num;
    out_buf[0] = 0x12; /* DTC hi */
    out_buf[1] = 0x34;
    out_buf[2] = 0x56;
    out_buf[3] = 0x09; /* statusOfDTC */
    out_buf[4] = 0x00; /* numberOfIdentifiers */
    return 5;
}

static int mock_dtc_extdata_by_record(struct uds_ctx *ctx, uint8_t record_num, uint8_t *out_buf,
                                      uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    g_seen_record_num = record_num;
    out_buf[0] = 0x12;
    out_buf[1] = 0x34;
    out_buf[2] = 0x56;
    out_buf[3] = 0x09; /* statusOfDTC */
    out_buf[4] = 0x2A; /* extended data */
    return 5;
}

static void test_read_dtc_info_0x05_stored_data(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_stored_data = mock_dtc_stored_data;

    uint8_t req[] = {0x19, 0x05, 0x07};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8); /* 59 05 <recNum> + 5 payload */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_record_num, 0x07);
    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x05);
    assert_int_equal(g_tx_buf[2], 0x07); /* stored-data record number echoed */
    assert_int_equal(g_tx_buf[3], 0x12);
    assert_int_equal(g_tx_buf[6], 0x09);
}

static void test_read_dtc_info_0x16_extdata_by_record(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_extdata_by_record = mock_dtc_extdata_by_record;

    uint8_t req[] = {0x19, 0x16, 0xFE};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_seen_record_num, 0xFE);
    assert_int_equal(g_tx_buf[1], 0x16);
    assert_int_equal(g_tx_buf[2], 0xFE);
    assert_int_equal(g_tx_buf[7], 0x2A);
}

static void test_read_dtc_info_0x05_missing_record_number(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_stored_data = mock_dtc_stored_data;

    uint8_t req[] = {0x19, 0x05};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x13);
}

/* --- Back-compat: without the new hooks the raw fn_dtc_read path still runs --- */

static uint8_t g_raw_sub;

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req, uint16_t req_len,
                         uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) req;
    (void) req_len;
    (void) max_len;
    g_raw_sub = subfn;
    out_buf[0] = 0xAA;
    return 1;
}

static void test_read_dtc_info_0x18_falls_back_to_raw_hook(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read; /* no fn_dtc_snapshot_mem */
    g_raw_sub = 0u;

    uint8_t req[] = {0x19, 0x18, 0x12, 0x34, 0x56, 0x01, 0x33};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 59 18 AA */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 7);

    assert_int_equal(g_raw_sub, 0x18);
    assert_int_equal(g_tx_buf[1], 0x18);
    assert_int_equal(g_tx_buf[2], 0xAA);
}

/* --- Reference store wiring --- */

static void test_store_backed_mirror_and_emissions(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;

    uds_dtc_store_init(&store, backing, 4u, 40u);
    (void) uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                                  UDS_DTC_FGID_EMISSIONS);
    (void) uds_dtc_store_register(&store, 0x067890u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0x11u,
                                  UDS_DTC_FGID_SAFETY);
    uds_dtc_store_report_test(&store, 0x012345u, true);
    uds_dtc_store_report_test(&store, 0x067890u, true);

    cfg.app_data = &store;
    cfg.fn_dtc_list_mem = uds_dtc_store_list_mem_cb;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x13 emissions-related OBD: only the functional-group 0x33 DTC. */
    uint8_t req[] = {0x19, 0x13, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 7); /* 59 13 <avail> + 1 * 4 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[1], 0x13);
    assert_int_equal(g_tx_buf[3], 0x01);
    assert_int_equal(g_tx_buf[4], 0x23);
    assert_int_equal(g_tx_buf[5], 0x45);

    /* 0x0F mirror memory reports the whole set. */
    uint8_t req_mirror[] = {0x19, 0x0F, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 11); /* 59 0F <avail> + 2 * 4 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req_mirror, 3);

    assert_int_equal(g_tx_buf[1], 0x0F);
    assert_int_equal(g_tx_buf[7], 0x06);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_dtc_info_0x03_snapshot_identification),
        cmocka_unit_test(test_read_dtc_info_0x05_stored_data),
        cmocka_unit_test(test_read_dtc_info_0x05_missing_record_number),
        cmocka_unit_test(test_read_dtc_info_0x0F_mirror_by_status),
        cmocka_unit_test(test_read_dtc_info_0x0F_suppress_positive),
        cmocka_unit_test(test_read_dtc_info_0x10_mirror_extdata),
        cmocka_unit_test(test_read_dtc_info_0x11_number_of_mirror),
        cmocka_unit_test(test_read_dtc_info_0x12_number_of_emissions),
        cmocka_unit_test(test_read_dtc_info_0x13_emissions_by_status),
        cmocka_unit_test(test_read_dtc_info_0x16_extdata_by_record),
        cmocka_unit_test(test_read_dtc_info_0x17_user_def_by_status),
        cmocka_unit_test(test_read_dtc_info_0x17_missing_memory_selection),
        cmocka_unit_test(test_read_dtc_info_0x18_user_def_snapshot),
        cmocka_unit_test(test_read_dtc_info_0x18_missing_memory_selection),
        cmocka_unit_test(test_read_dtc_info_0x19_user_def_extdata),
        cmocka_unit_test(test_read_dtc_info_0x18_falls_back_to_raw_hook),
        cmocka_unit_test(test_store_backed_mirror_and_emissions),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
