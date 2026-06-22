/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_coverage2.c
 * @brief Second batch of regression coverage for reachable error/negative
 *        branches: data services (0x22/0x24/0x2A/0x2C/0x2E), DTC maintenance
 *        error paths (0x19 list-negative / response-too-long), flash request
 *        download/upload (0x34/0x35), link (0x87) length errors, security
 *        (0x27) handler-NRC, and access timing (0x83) success readback.
 */

#include "test_helpers.h"
#include "uds_internal.h"

static void expect_nrc(uds_ctx_t *ctx, uint8_t *req, uint16_t len, uint8_t sid, uint8_t nrc,
                       int extra_time)
{
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    if (extra_time) {
        will_return(mock_get_time, 1000);
    }
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, req, len);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], sid);
    assert_int_equal(g_tx_buf[2], nrc);
}

/* ---- 0x22 Read Data By ID ---- */

/* A DID with neither read handler nor storage -> conditionsNotCorrect (data.c:72-74). */
static const uds_did_entry_t misconfigured_did[] = {
    {0xABCDu, 2u, 0u, 0u, NULL, NULL, NULL}, /* no read, no storage */
};
static void test_rdbi_misconfigured_did(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table.entries = misconfigured_did;
    cfg.did_table.count = 1u;
    uint8_t req[] = {0x22, 0xAB, 0xCD};
    expect_nrc(&ctx, req, sizeof(req), 0x22, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ---- 0x24 Read Scaling ---- */

static int scaling_negative(struct uds_ctx *ctx, uint16_t did, uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) did;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}
/* fn_read_scaling negative -> its NRC (data.c:110-112). */
static void test_scaling_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_read_scaling = scaling_negative;
    uint8_t req[] = {0x24, 0xF1, 0x90};
    expect_nrc(&ctx, req, sizeof(req), 0x24, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* ---- 0x2C Dynamic DID ---- */

static int dyn_negative(struct uds_ctx *ctx, uint8_t subfn, uint16_t did, const uint8_t *data,
                        uint16_t len)
{
    (void) ctx;
    (void) subfn;
    (void) did;
    (void) data;
    (void) len;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}
/* fn_dynamic_did negative -> its NRC (data.c:146-148). */
static void test_dynamic_did_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = dyn_negative;
    uint8_t req[] = {0x2C, 0x01, 0xF2, 0x00};
    expect_nrc(&ctx, req, sizeof(req), 0x2C, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* ---- 0x2E Write Data By ID ---- */

static const uds_did_entry_t wdbi_session_did[] = {
    {0xD100u, 1u, (uint8_t) UDS_SESSION_PROGRAMMING, 0u, NULL, NULL, NULL},
};
/* WDBI in a session the DID does not allow -> requestOutOfRange (data.c:180-183). */
static void test_wdbi_session_denied(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.did_table.entries = wdbi_session_did;
    cfg.did_table.count = 1u;
    uint8_t req[] = {0x2E, 0xD1, 0x00, 0xAA}; /* default session != programming */
    expect_nrc(&ctx, req, sizeof(req), 0x2E, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* ---- 0x2A Periodic ---- */

static int periodic_read_ok(struct uds_ctx *ctx, uint8_t pid, uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) pid;
    (void) max;
    out[0] = 0x11;
    return 1;
}

/* Stop-all (mode 0x04, no IDs) clears the table (data.c:234-237). */
static void test_periodic_stop_all(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = periodic_read_ok;

    /* First add a periodic ID at fast rate. */
    uint8_t add[] = {0x2A, 0x01, 0x55};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000); /* handler reads time when registering */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, add, sizeof(add));
    assert_int_equal(ctx.server.periodic_count, 1u);

    /* Re-add the same ID at a different rate -> update existing (data.c:272-274). */
    uint8_t readd[] = {0x2A, 0x02, 0x55};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, readd, sizeof(readd));
    assert_int_equal(ctx.server.periodic_count, 1u); /* still one slot */

    /* Stop all. */
    uint8_t stop[] = {0x2A, 0x04};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, stop, sizeof(stop));
    assert_int_equal(ctx.server.periodic_count, 0u);
}

/* Adding a ninth periodic ID overflows the table -> responseTooLong (data.c:279-281). */
static void test_periodic_table_full(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = periodic_read_ok;

    /* One request carrying 8 distinct IDs fills the table. */
    uint8_t fill[] = {0x2A, 0x01, 1, 2, 3, 4, 5, 6, 7, 8};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    for (int i = 0; i < 8; i++) {
        will_return(mock_get_time, 1000); /* one per registered ID */
    }
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, fill, sizeof(fill));
    assert_int_equal(ctx.server.periodic_count, 8u);

    /* A 9th unique ID cannot fit. */
    uint8_t over[] = {0x2A, 0x01, 9};
    expect_nrc(&ctx, over, sizeof(over), 0x2A, UDS_NRC_RESPONSE_TOO_LONG, 0);
}

/* ---- 0x87 Link Control length errors ---- */

static int link_ok2(struct uds_ctx *ctx, uint8_t sub, uint32_t param)
{
    (void) ctx;
    (void) sub;
    (void) param;
    return 0;
}

/* sub 0x01 with len<3 -> incorrectLength (link.c:34-35). */
static void test_link_verify_fixed_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_ok2;
    uint8_t req[] = {0x87, 0x01}; /* missing the mode identifier byte */
    expect_nrc(&ctx, req, sizeof(req), 0x87, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* sub 0x02 with len<5 -> incorrectLength (link.c:41-42). */
static void test_link_verify_specific_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_ok2;
    uint8_t req[] = {0x87, 0x02, 0x11}; /* missing 2 baud bytes */
    expect_nrc(&ctx, req, sizeof(req), 0x87, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* verify then transition where the transition callback fails (link.c:69-72). */
static int link_verify_then_fail(struct uds_ctx *ctx, uint8_t sub, uint32_t param)
{
    (void) ctx;
    (void) param;
    return (sub == 0x03u) ? -(int) UDS_NRC_CONDITIONS_NOT_CORRECT : 0;
}
static void test_link_transition_callback_error(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_verify_then_fail;

    /* verify (0x01) succeeds and latches. */
    uint8_t verify[] = {0x87, 0x01, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, verify, sizeof(verify));
    assert_true(ctx.server.link_ctrl_verified);

    /* transition (0x03) now fails in the callback. */
    uint8_t trans[] = {0x87, 0x03};
    expect_nrc(&ctx, trans, sizeof(trans), 0x87, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ---- 0x83 Access Timing readback ---- */

/* sub 0x03 reads back the currently active P2/P2* timing (link.c:93-104). */
static void test_access_timing_readback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.session.p2_ms = 0x0032;      /* 50 */
    ctx.session.p2_star_ms = 0x1388; /* 5000 -> /10 = 500 = 0x01F4 */

    uint8_t req[] = {0x83, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC3);
    assert_int_equal(g_tx_buf[1], 0x03);
    assert_int_equal(g_tx_buf[3], 0x32);
    assert_int_equal(g_tx_buf[5], 0xF4);
}

/* ---- 0x27 Security handler NRC (custom key NRC) ---- */

static int seed_ok(struct uds_ctx *ctx, uint8_t level, uint8_t *seed, uint16_t max)
{
    (void) ctx;
    (void) level;
    (void) max;
    seed[0] = 0x01;
    seed[1] = 0x02;
    return 2;
}
static int key_custom_nrc(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                          const uint8_t *key, uint16_t key_len)
{
    (void) ctx;
    (void) level;
    (void) seed;
    (void) key;
    (void) key_len;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE; /* handler-provided NRC, not 0x35 */
}

/* A failed key with attempts below max returns the handler's own NRC (security.c:117-119). */
static void test_security_key_handler_nrc(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = seed_ok;
    cfg.fn_security_key = key_custom_nrc;
    cfg.security_max_attempts = 5; /* stay below the lockout threshold */

    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 67 01 + 2-byte seed */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));

    uint8_t key_req[] = {0x27, 0x02, 0xFF, 0xFF};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, key_req, sizeof(key_req));
    assert_int_equal(g_tx_buf[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
}

/* ---- 0x34 / 0x35 download/upload error paths ---- */

static int download_negative(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    (void) addr;
    (void) size;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}
static int upload_negative(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    (void) addr;
    (void) size;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* Bad ALFID on request download -> requestOutOfRange (flash.c:66-67). */
static void test_download_bad_alfid(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = download_negative;
    /* 0x34 <dataFormat=0x00> <ALFID=0x10 (size nibble 0)> <addr> */
    uint8_t req[] = {0x34, 0x00, 0x10, 0x20};
    expect_nrc(&ctx, req, sizeof(req), 0x34, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* parse_addr_len failure (too short for ALFID) on request download -> incorrectLength
 * (flash.c:73-74). */
static void test_download_addr_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = download_negative;
    /* ALFID 0x22 needs 2 addr + 2 size = 4 bytes after it, but only 1 provided. */
    uint8_t req[] = {0x34, 0x00, 0x22, 0x20};
    expect_nrc(&ctx, req, sizeof(req), 0x34, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* fn_request_download negative -> its NRC (flash.c:83-85). */
static void test_download_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = download_negative;
    /* ALFID 0x11: 1 addr + 1 size byte. */
    uint8_t req[] = {0x34, 0x00, 0x11, 0x20, 0x10};
    expect_nrc(&ctx, req, sizeof(req), 0x34, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_request_upload negative -> its NRC (flash.c:242-244). */
static void test_upload_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_upload = upload_negative;
    uint8_t req[] = {0x35, 0x00, 0x11, 0x20, 0x10};
    expect_nrc(&ctx, req, sizeof(req), 0x35, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* Request upload with no callback -> conditionsNotCorrect (flash.c:236-237 region). */
static void test_upload_no_callback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x35, 0x00, 0x11, 0x20, 0x10};
    expect_nrc(&ctx, req, sizeof(req), 0x35, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* parse_addr_len failure for read memory (ALFID too long for payload) ->
 * incorrectLength (mem.c:33-36). */
static int mem_read_ok2(struct uds_ctx *ctx, uint32_t addr, uint32_t size, uint8_t *out)
{
    (void) ctx;
    (void) addr;
    memset(out, 0, size);
    return 0;
}
static void test_mem_read_addr_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_read = mem_read_ok2;
    /* ALFID 0x22 needs 4 trailing bytes; only 1 supplied. */
    uint8_t req[] = {0x23, 0x22, 0x10};
    expect_nrc(&ctx, req, sizeof(req), 0x23, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* Write memory: parse_addr_len failure / length mismatch -> incorrectLength
 * (mem.c:80-88). */
static int mem_write_ok2(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data)
{
    (void) ctx;
    (void) addr;
    (void) size;
    (void) data;
    return 0;
}
static void test_mem_write_length_mismatch(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_write = mem_write_ok2;
    /* ALFID 0x11: consumed=4, size byte=0x04 so total must be 8; provide only 5. */
    uint8_t req[] = {0x3D, 0x11, 0x20, 0x04, 0xAA};
    expect_nrc(&ctx, req, sizeof(req), 0x3D, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* Write memory where parse_addr_len itself fails: ALFID claims 4 addr + 4 size
 * bytes but the payload is too short (mem.c:80-82). */
static void test_mem_write_addr_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_write = mem_write_ok2;
    /* ALFID 0x44 needs 8 trailing bytes; only 1 supplied. */
    uint8_t req[] = {0x3D, 0x44, 0x20};
    expect_nrc(&ctx, req, sizeof(req), 0x3D, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* Entering the safetySystemDiagnosticSession (0x04) exercises the SAFETY case
 * of uds_internal_session_bit (uds_core.c:187-189). */
static void test_session_safety(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x10, 0x04}; /* safetySystemDiagnosticSession */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(ctx.session.active, 0x04u);

    /* Now a service in this session goes through is_session_supported with the
     * SAFETY bit set: a TesterPresent succeeds. */
    uint8_t tp[] = {0x3E, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, tp, sizeof(tp));
    assert_int_equal(g_tx_buf[0], 0x7E);
}

/* ---- 0x19 maintenance error paths ---- */

static int dtc_list_negative(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* fn_dtc_list negative on a 0x01 number-by-status request -> its NRC
 * (maintenance.c:148-150). */
static void test_dtc_list_negative_0x01(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x01, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on a 0x02 by-status request -> its NRC (maintenance.c:166-167). */
static void test_dtc_list_negative_0x02(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x02, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x07 by-severity -> its NRC (maintenance.c:242-243). */
static void test_dtc_list_negative_0x07(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x07, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x09 severity info -> its NRC (maintenance.c:306-307). */
static void test_dtc_list_negative_0x09(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x09, 0x12, 0x34, 0x56};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x14 fault counter -> its NRC (maintenance.c:342-343). */
static void test_dtc_list_negative_0x14(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x14};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x42 WWH-OBD -> its NRC (maintenance.c:388-389). */
static void test_dtc_list_negative_0x42(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x55 permanent -> its NRC (maintenance.c:440-441). */
static void test_dtc_list_negative_0x55(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x55, 0x33};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x0B first-or-recent -> its NRC (maintenance.c:489-490). */
static void test_dtc_list_negative_0x0B(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x0B};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_dtc_list negative on 0x15 permanent -> its NRC (maintenance.c:536-537). */
static void test_dtc_list_negative_0x15(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = dtc_list_negative;
    uint8_t req[] = {0x19, 0x15};
    expect_nrc(&ctx, req, sizeof(req), 0x19, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ECU reset with the suppress bit set still runs the reset and emits no
 * positive response (maintenance.c reset path with suppress). */
static int g_reset_calls;
static void reset_cb(struct uds_ctx *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    g_reset_calls++;
}
static void test_ecu_reset_suppressed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_reset = reset_cb;
    g_reset_calls = 0;

    uint8_t req[] = {0x11, 0x81}; /* sub 0x01 + suppress bit */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No tp_send expected: positive response is suppressed. */
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_reset_calls, 1); /* deferred reset still runs */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_misconfigured_did),
        cmocka_unit_test(test_scaling_negative),
        cmocka_unit_test(test_dynamic_did_negative),
        cmocka_unit_test(test_wdbi_session_denied),
        cmocka_unit_test(test_periodic_stop_all),
        cmocka_unit_test(test_periodic_table_full),
        cmocka_unit_test(test_link_verify_fixed_short),
        cmocka_unit_test(test_link_verify_specific_short),
        cmocka_unit_test(test_link_transition_callback_error),
        cmocka_unit_test(test_access_timing_readback),
        cmocka_unit_test(test_security_key_handler_nrc),
        cmocka_unit_test(test_download_bad_alfid),
        cmocka_unit_test(test_download_addr_short),
        cmocka_unit_test(test_download_callback_negative),
        cmocka_unit_test(test_upload_callback_negative),
        cmocka_unit_test(test_upload_no_callback),
        cmocka_unit_test(test_mem_read_addr_short),
        cmocka_unit_test(test_mem_write_length_mismatch),
        cmocka_unit_test(test_mem_write_addr_short),
        cmocka_unit_test(test_session_safety),
        cmocka_unit_test(test_dtc_list_negative_0x01),
        cmocka_unit_test(test_dtc_list_negative_0x02),
        cmocka_unit_test(test_dtc_list_negative_0x07),
        cmocka_unit_test(test_dtc_list_negative_0x09),
        cmocka_unit_test(test_dtc_list_negative_0x14),
        cmocka_unit_test(test_dtc_list_negative_0x42),
        cmocka_unit_test(test_dtc_list_negative_0x55),
        cmocka_unit_test(test_dtc_list_negative_0x0B),
        cmocka_unit_test(test_dtc_list_negative_0x15),
        cmocka_unit_test(test_ecu_reset_suppressed),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
