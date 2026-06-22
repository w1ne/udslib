/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_coverage3.c
 * @brief Third batch: DTC maintenance overflow / length / negative branches
 *        (0x19 subfunctions 0x04/0x06/0x07/0x08/0x09/0x14/0x42/0x55/0x0B/0x15),
 *        the legacy fn_dtc_read negative path, flash transfer sequence errors,
 *        and file transfer (0x38) negative.
 */

#include "test_helpers.h"
#include "uds_internal.h"
#include "uds/uds_dtc.h"

static void expect_nrc(uds_ctx_t *ctx, uint8_t *req, uint16_t len, uint8_t sid, uint8_t nrc)
{
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, req, len);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], sid);
    assert_int_equal(g_tx_buf[2], nrc);
}

/* A list callback that claims more records than the library's batch capacity,
 * triggering the responseTooLong guard in every structured subhandler. */
static int dtc_list_overflow(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    (void) out;
    (void) max;
    return 1000; /* far exceeds UDS_DTC_LIST_BATCH (32) */
}

static void test_dtc_overflow_0x02(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x02, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x08(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x08, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x09(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x09, 0x12, 0x34, 0x56};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x14(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x14};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x42(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x55(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x55, 0x33};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x0B(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x0B};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}
static void test_dtc_overflow_0x15(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_overflow;
    uint8_t req[] = {0x19, 0x15};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* 0x07 by-severity with a too-short request -> incorrectLength (maintenance.c:233). */
static int dtc_list_one(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                        uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    if (out != NULL && max > 0u) {
        out[0].dtc = 0x123456u;
        out[0].status = 0x08u;
        out[0].severity = 0x80u;
        out[0].functional_unit = 0x10u;
        out[0].fault_detection_counter = 0;
        out[0].functional_group = UDS_DTC_FGID_EMISSIONS;
    }
    return 1;
}
static void test_dtc_severity_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    uint8_t req[] = {0x19, 0x07, 0xFF}; /* len 3, by_severity needs >= 4 */
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_INCORRECT_LENGTH);
}

/* 0x09 severity info with a too-short request -> incorrectLength (maintenance.c:297). */
static void test_dtc_severity_info_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    uint8_t req[] = {0x19, 0x09, 0x12, 0x34}; /* len 4, needs >= 5 */
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_INCORRECT_LENGTH);
}

/* Snapshot callback returns negative -> its NRC (maintenance.c:214-216). */
static int snapshot_negative(struct uds_ctx *ctx, uint32_t dtc, uint8_t rec, uint8_t *out,
                             uint16_t max)
{
    (void) ctx;
    (void) dtc;
    (void) rec;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}
static void test_dtc_snapshot_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_snapshot = snapshot_negative;
    uint8_t req[] = {0x19, 0x04, 0x12, 0x34, 0x56, 0x01};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

/* Legacy fn_dtc_read returns negative -> its NRC (maintenance.c:653-655). */
static int dtc_read_negative(struct uds_ctx *ctx, uint8_t sub, const uint8_t *req, uint16_t len,
                             uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) sub;
    (void) req;
    (void) len;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}
static void test_dtc_read_legacy_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = dtc_read_negative; /* no fn_dtc_list -> legacy path */
    uint8_t req[] = {0x19, 0x0F, 0xFF};  /* niche sub routes to legacy hook */
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

/* Per-record buffer overflow in 0x08 (small tx buffer) -> responseTooLong
 * (maintenance.c:278-279). */
static void test_dtc_0x08_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    cfg.tx_buffer_size = 6u; /* room for header only; first record overflows */
    uint8_t req[] = {0x19, 0x08, 0x80, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* ---- 0x36 transfer sequence error (out-of-order block) ---- */

static int transfer_ok3(struct uds_ctx *ctx, uint8_t seq, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) seq;
    (void) data;
    (void) len;
    return 0;
}
/* After accepting block 1, a non-consecutive block 5 is a wrong block sequence
 * counter error (flash.c:136-137 develop conformance: 0x73, not 0x24). */
static void test_transfer_out_of_order(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = transfer_ok3;
    ctx.server.transfer_active = true; /* arm transfer (develop conformance) */

    uint8_t b1[] = {0x36, 0x01, 0xAA};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, b1, sizeof(b1));
    assert_int_equal(ctx.server.flash_sequence, 0x01u);

    /* Expected next is 0x02; send 0x05 -> wrongBlockSequenceCounter (0x73).
     * Develop conformance: active transfer + wrong counter -> 0x73, not 0x24. */
    uint8_t b5[] = {0x36, 0x05, 0xBB};
    expect_nrc(&ctx, b5, sizeof(b5), 0x36, 0x73u);
}

/* ---- 0x38 file transfer negative ---- */

static int file_transfer_negative(struct uds_ctx *ctx, uint8_t mode, const uint8_t *path,
                                  uint16_t path_len, const uint8_t *params, uint16_t params_len,
                                  uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) mode;
    (void) path;
    (void) path_len;
    (void) params;
    (void) params_len;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}
static void test_file_transfer_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_file_transfer = file_transfer_negative;
    /* 0x38 <mode=1> <pathLen=0x0002> <path 2 bytes> */
    uint8_t req[] = {0x38, 0x01, 0x00, 0x02, 0x41, 0x42};
    expect_nrc(&ctx, req, sizeof(req), 0x38, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

/* 0x42 WWH-OBD with a too-short request -> incorrectLength (maintenance.c:378-379). */
static void test_dtc_wwhobd_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF}; /* len 4, needs >= 5 */
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_INCORRECT_LENGTH);
}

/* 0x55 WWH-OBD-permanent with a too-short request -> incorrectLength
 * (maintenance.c:432-433). */
static void test_dtc_wwhobd_permanent_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    uint8_t req[] = {0x19, 0x55}; /* len 2, needs >= 3 */
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_INCORRECT_LENGTH);
}

/* A confirmed DTC in the requested functional group, matched by 0x55 permanent
 * (maintenance.c happy path) and a record skipped by the FGID/confirmed filters
 * (the continue branches). Two records: one matches, one is skipped. */
static int dtc_list_two_mixed(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                              uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    if (out != NULL && max >= 2u) {
        /* Confirmed, emissions group -> reported by 0x55 with FGID 0x33. */
        out[0].dtc = 0xA00001u;
        out[0].status = UDS_DTC_STATUS_CONFIRMED;
        out[0].severity = 0x80u;
        out[0].functional_unit = 0;
        out[0].fault_detection_counter = 0;
        out[0].functional_group = UDS_DTC_FGID_EMISSIONS;
        /* Not confirmed -> skipped by the confirmed filter (continue branch). */
        out[1].dtc = 0xA00002u;
        out[1].status = 0x01u;
        out[1].severity = 0x40u;
        out[1].functional_unit = 0;
        out[1].fault_detection_counter = 0;
        out[1].functional_group = UDS_DTC_FGID_EMISSIONS;
    }
    return 2;
}
static void test_dtc_0x55_skips_unconfirmed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_two_mixed;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x04u;

    uint8_t req[] = {0x19, 0x55, 0x33};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 55 FGID statAvail format + 1*(DTC[3] status) = 5 + 4 = 9 (only the
     * confirmed record). */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[1], 0x55);
    assert_int_equal(g_tx_buf[5], 0xA0); /* the single confirmed DTC */
}

/* 0x42 per-record buffer overflow with a tiny tx buffer (maintenance.c:413-414). */
static void test_dtc_0x42_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one;
    cfg.tx_buffer_size = 7u; /* header is 6 bytes; first record (5) overflows */
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* Fault-counter (0x14) record with fdc in range but a tiny tx buffer ->
 * responseTooLong (maintenance.c:358-360). */
static int dtc_list_fdc(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                        uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    if (out != NULL && max > 0u) {
        out[0].dtc = 0x111111u;
        out[0].status = 0x04u;
        out[0].severity = 0;
        out[0].functional_unit = 0;
        out[0].fault_detection_counter = 0x20; /* in 1..0x7E -> reported */
        out[0].functional_group = 0;
    }
    return 1;
}
static void test_dtc_0x14_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_fdc;
    cfg.tx_buffer_size = 3u; /* header 2 bytes; first record (4) overflows */
    uint8_t req[] = {0x19, 0x14};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* first-or-recent (0x0B) selected record with a tiny tx buffer ->
 * responseTooLong (maintenance.c:514-516). */
static void test_dtc_0x0B_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one; /* status 0x08, has testFailed? no -> use confirmed */
    cfg.tx_buffer_size = 5u;        /* header 3; record (4) overflows */
    /* 0x0C selects first confirmed; dtc_list_one record has status 0x08 (confirmed). */
    uint8_t req[] = {0x19, 0x0C};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* permanent (0x15) confirmed record with a tiny tx buffer -> responseTooLong
 * (maintenance.c:554-556). */
static void test_dtc_0x15_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one; /* status 0x08 confirmed */
    cfg.tx_buffer_size = 5u;
    uint8_t req[] = {0x19, 0x15};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* 0x55 confirmed record with a tiny tx buffer -> responseTooLong
 * (maintenance.c:463-465). */
static void test_dtc_0x55_record_overflow(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one; /* confirmed (0x08), emissions group */
    cfg.tx_buffer_size = 6u;        /* header 5; record (4) overflows */
    uint8_t req[] = {0x19, 0x55, 0x33};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_RESPONSE_TOO_LONG);
}

/* 0x42 skips a record whose functional group does not match the FGID
 * (maintenance.c:409-410 continue). The single record is emissions (0x33), so
 * a request for FGID 0xD0 yields an empty (header-only) positive response. */
static void test_dtc_0x42_fgid_skip(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_one; /* functional_group = EMISSIONS (0x33) */
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_severity_availability_mask = 0xE0u;
    cfg.dtc_format_id = 0x04u;

    uint8_t req[] = {0x19, 0x42, 0xD0, 0xFF, 0xFF}; /* FGID 0xD0 != record's 0x33 */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* header only, record skipped */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[1], 0x42);
    assert_int_equal(g_tx_buf[2], 0xD0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dtc_0x14_record_overflow),
        cmocka_unit_test(test_dtc_0x0B_record_overflow),
        cmocka_unit_test(test_dtc_0x15_record_overflow),
        cmocka_unit_test(test_dtc_0x55_record_overflow),
        cmocka_unit_test(test_dtc_0x42_fgid_skip),
        cmocka_unit_test(test_dtc_overflow_0x02),
        cmocka_unit_test(test_dtc_overflow_0x08),
        cmocka_unit_test(test_dtc_overflow_0x09),
        cmocka_unit_test(test_dtc_overflow_0x14),
        cmocka_unit_test(test_dtc_overflow_0x42),
        cmocka_unit_test(test_dtc_overflow_0x55),
        cmocka_unit_test(test_dtc_overflow_0x0B),
        cmocka_unit_test(test_dtc_overflow_0x15),
        cmocka_unit_test(test_dtc_severity_short),
        cmocka_unit_test(test_dtc_severity_info_short),
        cmocka_unit_test(test_dtc_snapshot_negative),
        cmocka_unit_test(test_dtc_read_legacy_negative),
        cmocka_unit_test(test_dtc_0x08_record_overflow),
        cmocka_unit_test(test_transfer_out_of_order),
        cmocka_unit_test(test_file_transfer_negative),
        cmocka_unit_test(test_dtc_wwhobd_short),
        cmocka_unit_test(test_dtc_wwhobd_permanent_short),
        cmocka_unit_test(test_dtc_0x55_skips_unconfirmed),
        cmocka_unit_test(test_dtc_0x42_record_overflow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
