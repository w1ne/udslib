/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_coverage.c
 * @brief Regression coverage for negative/error branches across the service
 *        handlers that the per-SID tests do not exercise: session (0x10),
 *        security (0x27/0x29), IO control (0x2F), link control (0x87),
 *        access timing (0x83), memory (0x23/0x3D), flash (0x31/0x34/0x36/0x37),
 *        maintenance (0x11/0x28/0x14), and ResponseOnEvent (0x86).
 *
 * Every uds_input_sdu() call consumes two mock_get_time values (last_msg_time
 * and p2_timer_start). Services that read the clock internally (0x27) need a
 * third.
 */

#include "test_helpers.h"
#include "uds_internal.h"

/* Drive a single request and expect a 3-byte negative response with `nrc`. */
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

/* ---- 0x10 Session Control ---- */

/* An out-of-range session ID is rejected (uds_service_session.c:21-23). */
static void test_session_invalid_id(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x10, 0x06}; /* 0x06 is not a valid session */
    expect_nrc(&ctx, req, sizeof(req), 0x10, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, 0);
}

/* Configured p2/p2* server-max values are echoed in the response (:56-57). */
static void test_session_uses_configured_p2(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.p2_server_max = 0x0064;      /* 100ms */
    cfg.p2_star_server_max = 0x2710; /* 10000ms -> /10 = 1000 = 0x03E8 */

    uint8_t req[] = {0x10, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x50);
    assert_int_equal(g_tx_buf[2], 0x00);
    assert_int_equal(g_tx_buf[3], 0x64); /* p2 = 100 */
    assert_int_equal(g_tx_buf[4], 0x03);
    assert_int_equal(g_tx_buf[5], 0xE8); /* p2* /10 = 1000 */
}

/* ---- 0x27 Security Access ---- */

static int seed_negative(struct uds_ctx *ctx, uint8_t level, uint8_t *seed, uint16_t max)
{
    (void) ctx;
    (void) level;
    (void) seed;
    (void) max;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* fn_security_seed returning negative yields its NRC (uds_service_security.c:51-53). */
static void test_security_seed_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = seed_negative;
    uint8_t req[] = {0x27, 0x01};
    expect_nrc(&ctx, req, sizeof(req), 0x27, UDS_NRC_CONDITIONS_NOT_CORRECT, 1);
}

/* ---- 0x29 Authentication ---- */

static int auth_negative(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                         uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) subfn;
    (void) data;
    (void) len;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_INVALID_KEY;
}

/* fn_auth returning negative yields its NRC (uds_service_security.c:171-173). */
static void test_auth_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = auth_negative;
    uint8_t req[] = {0x29, 0x01, 0xAA}; /* sub 0x01 -> delegated to fn_auth */
    expect_nrc(&ctx, req, sizeof(req), 0x29, UDS_NRC_INVALID_KEY, 0);
}

/* ---- 0x2F IO Control ---- */

static const uds_did_entry_t io_dids[] = {
    {0xD001u, 0u, (uint8_t) UDS_SESSION_PROGRAMMING, 0u, NULL, NULL, NULL}, /* session-gated */
    {0xD002u, 0u, 0u, 0x04u, NULL, NULL, NULL},                             /* security-gated */
    {0xD003u, 0u, 0u, 0u, NULL, NULL, NULL},                                /* open */
};

static int io_negative(struct uds_ctx *ctx, uint16_t id, uint8_t type, const uint8_t *data,
                       uint16_t len, uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) id;
    (void) type;
    (void) data;
    (void) len;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}

/* DID session-mask mismatch -> serviceNotSupportedInActiveSession (:40-42). */
static void test_io_session_denied(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_io_control = io_negative;
    cfg.did_table.entries = io_dids;
    cfg.did_table.count = 3u;
    /* default session (0x01) does not satisfy PROGRAMMING mask */
    uint8_t req[] = {0x2F, 0xD0, 0x01, 0x03};
    expect_nrc(&ctx, req, sizeof(req), 0x2F, UDS_NRC_SERVICE_NOT_SUPP_IN_SESS, 0);
}

/* DID security-mask mismatch -> securityAccessDenied (:44-46). */
static void test_io_security_denied(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_io_control = io_negative;
    cfg.did_table.entries = io_dids;
    cfg.did_table.count = 3u;
    uint8_t req[] = {0x2F, 0xD0, 0x02, 0x03}; /* security_mask 0x04, level 0 */
    expect_nrc(&ctx, req, sizeof(req), 0x2F, UDS_NRC_SECURITY_ACCESS_DENIED, 0);
}

/* fn_io_control negative -> its NRC (:56-58). */
static void test_io_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_io_control = io_negative;
    cfg.did_table.entries = io_dids;
    cfg.did_table.count = 3u;
    uint8_t req[] = {0x2F, 0xD0, 0x03, 0x03}; /* open DID */
    expect_nrc(&ctx, req, sizeof(req), 0x2F, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* ---- 0x87 Link Control ---- */

static int g_link_param;
static int link_ok(struct uds_ctx *ctx, uint8_t sub, uint32_t param)
{
    (void) ctx;
    (void) sub;
    g_link_param = (int) param;
    return 0;
}
static int link_fail(struct uds_ctx *ctx, uint8_t sub, uint32_t param)
{
    (void) ctx;
    (void) sub;
    (void) param;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* sub 0x02 decodes the 3-byte baud parameter and succeeds (:40-45,:54-59). */
static void test_link_verify_specific_baud(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_ok;
    g_link_param = -1;

    uint8_t req[] = {0x87, 0x02, 0x11, 0x22, 0x33};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC7);
    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_link_param, 0x112233);
    assert_true(ctx.link_ctrl_verified);
}

/* fn_link_control negative on verify -> its NRC (:48-51). */
static void test_link_verify_callback_error(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_fail;
    uint8_t req[] = {0x87, 0x01, 0x03};
    expect_nrc(&ctx, req, sizeof(req), 0x87, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* transition (0x03) without a prior verify -> requestSequenceError (:64-65). */
static void test_link_transition_without_verify(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = link_ok;
    uint8_t req[] = {0x87, 0x03};
    expect_nrc(&ctx, req, sizeof(req), 0x87, UDS_NRC_REQUEST_SEQUENCE_ERROR, 0);
}

/* ---- 0x83 Access Timing ---- */

/* sub 0x04 sets timing to the given values (:115-125). */
static void test_access_timing_set_given(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x83, 0x04, 0x00, 0x96, 0x00, 0x64}; /* P2=150, P2*=100*10 */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC3);
    assert_int_equal(g_tx_buf[1], 0x04);
    assert_int_equal(ctx.p2_ms, 150u);
    assert_int_equal(ctx.p2_star_ms, 1000u);
}

/* sub 0x04 with a too-short request -> incorrectLength (:116-118). */
static void test_access_timing_set_given_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x83, 0x04, 0x00, 0x96}; /* only 4 bytes, need 6 */
    expect_nrc(&ctx, req, sizeof(req), 0x83, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* ---- 0x23 / 0x3D Memory ---- */

static int mem_read_negative(struct uds_ctx *ctx, uint32_t addr, uint32_t size, uint8_t *out)
{
    (void) ctx;
    (void) addr;
    (void) size;
    (void) out;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}
static int mem_read_ok(struct uds_ctx *ctx, uint32_t addr, uint32_t size, uint8_t *out)
{
    (void) ctx;
    (void) addr;
    memset(out, 0xEE, size);
    return 0;
}
static int mem_write_negative(struct uds_ctx *ctx, uint32_t addr, uint32_t size,
                              const uint8_t *data)
{
    (void) ctx;
    (void) addr;
    (void) size;
    (void) data;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}
static int mem_write_ok(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data)
{
    (void) ctx;
    (void) addr;
    (void) size;
    (void) data;
    return 0;
}

/* ALFID with a zero nibble -> requestOutOfRange (uds_service_mem.c:28-30). */
static void test_mem_read_bad_alfid(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_read = mem_read_ok;
    uint8_t req[] = {0x23, 0x01, 0x10}; /* size nibble 0 */
    expect_nrc(&ctx, req, sizeof(req), 0x23, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* Requested size exceeds the tx buffer -> responseTooLong (:44-46). */
static void test_mem_read_too_long(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_read = mem_read_ok;
    cfg.tx_buffer_size = 8u;                  /* size must fit in 7 */
    uint8_t req[] = {0x23, 0x11, 0x00, 0xFF}; /* addr 1B = 0x00, size 1B = 0xFF (255) */
    expect_nrc(&ctx, req, sizeof(req), 0x23, UDS_NRC_RESPONSE_TOO_LONG, 0);
}

/* fn_mem_read negative -> its NRC (:50-52). */
static void test_mem_read_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_read = mem_read_negative;
    uint8_t req[] = {0x23, 0x11, 0x10, 0x04}; /* addr 0x10, size 4 */
    expect_nrc(&ctx, req, sizeof(req), 0x23, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* Write without fn_mem_write -> conditionsNotCorrect (:90-92). */
static void test_mem_write_no_callback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* 0x3D, ALFID 0x11, addr(1)=0x20, size(1)=0x01, data(1)=0xAB */
    uint8_t req[] = {0x3D, 0x11, 0x20, 0x01, 0xAB};
    expect_nrc(&ctx, req, sizeof(req), 0x3D, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_is_safe veto on write -> conditionsNotCorrect (:95-98). */
static bool never_safe(struct uds_ctx *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) sid;
    (void) data;
    (void) len;
    return false;
}
static void test_mem_write_unsafe(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_write = mem_write_ok;
    cfg.fn_is_safe = never_safe;
    uint8_t req[] = {0x3D, 0x11, 0x20, 0x01, 0xAB};
    expect_nrc(&ctx, req, sizeof(req), 0x3D, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_mem_write negative -> its NRC (:101-104). */
static void test_mem_write_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_mem_write = mem_write_negative;
    uint8_t req[] = {0x3D, 0x11, 0x20, 0x01, 0xAB};
    expect_nrc(&ctx, req, sizeof(req), 0x3D, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ---- 0x31/0x34/0x36/0x37 Flash ---- */

static int routine_negative(struct uds_ctx *ctx, uint8_t type, uint16_t id, const uint8_t *data,
                            uint16_t len, uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) type;
    (void) id;
    (void) data;
    (void) len;
    (void) out;
    (void) max;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}

/* Routine control without fn_routine_control -> conditionsNotCorrect (:26-28). */
static void test_routine_no_callback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x31, 0x01, 0xFF, 0x00};
    expect_nrc(&ctx, req, sizeof(req), 0x31, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* fn_routine_control negative -> its NRC (:37-39). */
static void test_routine_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_routine_control = routine_negative;
    uint8_t req[] = {0x31, 0x01, 0xFF, 0x00};
    expect_nrc(&ctx, req, sizeof(req), 0x31, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* Transfer data without fn_transfer_data -> conditionsNotCorrect (:109-110). */
static void test_transfer_no_callback(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x36, 0x01, 0xAA};
    expect_nrc(&ctx, req, sizeof(req), 0x36, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

static int transfer_negative(struct uds_ctx *ctx, uint8_t seq, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) seq;
    (void) data;
    (void) len;
    return -(int) UDS_NRC_REQUEST_OUT_OF_RANGE;
}
static int transfer_ok(struct uds_ctx *ctx, uint8_t seq, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) seq;
    (void) data;
    (void) len;
    return 0;
}

/* Sequence counter rolls 0xFF -> 0x00 (:125). */
static void test_transfer_seq_rollover(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = transfer_ok;
    ctx.flash_sequence = 0xFFu; /* next expected = 0x00 */

    uint8_t req[] = {0x36, 0x00, 0xAB};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x76);
    assert_int_equal(g_tx_buf[1], 0x00);
    assert_int_equal(ctx.flash_sequence, 0x00u);
}

/* Last-block replay accepted without re-invoking the callback (:128-135). */
static void test_transfer_last_block_replay(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = transfer_negative; /* would fail if invoked */
    cfg.transfer_accept_last_block_replay = true;
    ctx.flash_sequence = 0x05u; /* replay of the last block */

    uint8_t req[] = {0x36, 0x05, 0xAB};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x76);
    assert_int_equal(g_tx_buf[1], 0x05);
}

/* fn_transfer_data negative -> its NRC (:142-144). */
static void test_transfer_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = transfer_negative;
    /* first block must be 0x01 */
    uint8_t req[] = {0x36, 0x01, 0xAB};
    expect_nrc(&ctx, req, sizeof(req), 0x36, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

static int exit_negative(struct uds_ctx *ctx)
{
    (void) ctx;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* fn_transfer_exit negative -> its NRC (:165-168). */
static void test_transfer_exit_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_exit = exit_negative;
    uint8_t req[] = {0x37};
    expect_nrc(&ctx, req, sizeof(req), 0x37, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ---- 0x11 ECU Reset / 0x28 Comm Control / 0x14 Clear DTC ---- */

/* ECU reset invalid sub-function -> subfunctionNotSupported (:28-30). */
static void test_ecu_reset_invalid_sub(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x11, 0x06}; /* valid range 0x01-0x05 */
    expect_nrc(&ctx, req, sizeof(req), 0x11, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, 0);
}

static int dtc_clear_negative(struct uds_ctx *ctx, uint32_t group)
{
    (void) ctx;
    (void) group;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

/* fn_dtc_clear negative -> its NRC (:117-119). */
static void test_clear_dtc_callback_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_clear = dtc_clear_negative;
    uint8_t req[] = {0x14, 0xFF, 0xFF, 0xFF};
    expect_nrc(&ctx, req, sizeof(req), 0x14, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

/* ---- 0x86 ResponseOnEvent ---- */

/* sub 0x03 (onChangeOfDID) with a too-short request -> incorrectLength (:40-42). */
static void test_roe_change_did_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x86, 0x03, 0x02, 0xF1}; /* len 4, need >= 6 */
    expect_nrc(&ctx, req, sizeof(req), 0x86, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* sub 0x07 (onComparisonOfValues) with a too-short request -> incorrectLength (:52-54). */
static void test_roe_comparison_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x86, 0x07, 0x02, 0x01}; /* len 4, need >= 9 */
    expect_nrc(&ctx, req, sizeof(req), 0x86, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* sub 0x01 with a too-short request -> incorrectLength (:62-64). */
static void test_roe_dtc_status_short(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t req[] = {0x86, 0x01, 0x02}; /* len 3, need >= 5 */
    expect_nrc(&ctx, req, sizeof(req), 0x86, UDS_NRC_INCORRECT_LENGTH, 0);
}

/* serviceToRespondTo == ROE/0x84 is rejected as out of range (:75-79). */
static void test_roe_nested_rejected(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* sub 0x01: window(1)=0x02, param(1)=0x08, STR(1)=0x86 (nested ROE). */
    uint8_t req[] = {0x86, 0x01, 0x02, 0x08, 0x86};
    expect_nrc(&ctx, req, sizeof(req), 0x86, UDS_NRC_REQUEST_OUT_OF_RANGE, 0);
}

/* The slot table fills after UDS_ROE_MAX_EVENTS setups; the next is rejected
 * with conditionsNotCorrect (:90-92). */
static void setup_one_roe(uds_ctx_t *ctx)
{
    /* sub 0x01, window infinite (0x02), DTC mask param, STR = {0x22,0xF1,0x90}. */
    uint8_t req[] = {0x86, 0x01, 0x02, 0x08, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_any(mock_tp_send, len);
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, req, sizeof(req));
}

static void test_roe_slot_full(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    for (int i = 0; i < (int) UDS_ROE_MAX_EVENTS; i++) {
        setup_one_roe(&ctx);
    }
    /* One more setup must be rejected: table is full. */
    uint8_t req[] = {0x86, 0x01, 0x02, 0x08, 0x22, 0xF1, 0x90};
    expect_nrc(&ctx, req, sizeof(req), 0x86, UDS_NRC_CONDITIONS_NOT_CORRECT, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_invalid_id),
        cmocka_unit_test(test_session_uses_configured_p2),
        cmocka_unit_test(test_security_seed_negative),
        cmocka_unit_test(test_auth_negative),
        cmocka_unit_test(test_io_session_denied),
        cmocka_unit_test(test_io_security_denied),
        cmocka_unit_test(test_io_callback_negative),
        cmocka_unit_test(test_link_verify_specific_baud),
        cmocka_unit_test(test_link_verify_callback_error),
        cmocka_unit_test(test_link_transition_without_verify),
        cmocka_unit_test(test_access_timing_set_given),
        cmocka_unit_test(test_access_timing_set_given_short),
        cmocka_unit_test(test_mem_read_bad_alfid),
        cmocka_unit_test(test_mem_read_too_long),
        cmocka_unit_test(test_mem_read_callback_negative),
        cmocka_unit_test(test_mem_write_no_callback),
        cmocka_unit_test(test_mem_write_unsafe),
        cmocka_unit_test(test_mem_write_callback_negative),
        cmocka_unit_test(test_routine_no_callback),
        cmocka_unit_test(test_routine_callback_negative),
        cmocka_unit_test(test_transfer_no_callback),
        cmocka_unit_test(test_transfer_seq_rollover),
        cmocka_unit_test(test_transfer_last_block_replay),
        cmocka_unit_test(test_transfer_callback_negative),
        cmocka_unit_test(test_transfer_exit_negative),
        cmocka_unit_test(test_ecu_reset_invalid_sub),
        cmocka_unit_test(test_clear_dtc_callback_negative),
        cmocka_unit_test(test_roe_change_did_short),
        cmocka_unit_test(test_roe_comparison_short),
        cmocka_unit_test(test_roe_dtc_status_short),
        cmocka_unit_test(test_roe_nested_rejected),
        cmocka_unit_test(test_roe_slot_full),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
