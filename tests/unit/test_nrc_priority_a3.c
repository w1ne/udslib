/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_nrc_priority_a3.c
 * @brief A3 task: per-service / NRC-priority / 0x84-combination regression tests.
 *
 * Locks the NRC ordering that handle_request() enforces (src/core/uds_core.c):
 *
 *   1. Service lookup (line 309): unknown SID -> 0x11 (or silent if functional)
 *   2. Addressing gate (line 319): wrong addr-mode -> silent/0x11
 *   3. Session (line 329): wrong session -> 0x7F
 *   4a. Sub-function length guard (line 340): sub-service but len<2 -> 0x13
 *   4b. Sub-function validity (line 346): known-length but bad sub -> 0x12
 *   5. Min-length (line 353): request too short -> 0x13
 *   6. Security (line 359): security_mask not met -> 0x33
 *   7. Auth hook (line 364): fn_auth_required true + !authenticated -> 0x34
 *   8. Safety hook (line 370): fn_is_safe returns false -> 0x22
 *
 * Each test comment cites the winning-condition line in uds_core.c.
 */

#include "test_helpers.h"
#include "uds_internal.h" /* uds_posttx_kind_t for the scratch-leak check */

/* ------------------------------------------------------------------ */
/* Shared helpers                                                       */
/* ------------------------------------------------------------------ */

static void dummy_ok(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_ok(out, 0u);
}

/* A non-conforming handler that returns WITHOUT describing a result. The
 * framework must fail closed and reject (generalReject 0x10), not emit a stray
 * positive from whatever is in tx_buffer. */
static void dummy_no_result(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) out;
}

static uds_service_entry_t k_svc_noresult[] = {
    {0xC2u, 1u, UDS_SESSION_ALL, 0u, dummy_no_result, NULL, 0u},
};

/*
 * Sub-mask: only subfunction 0x01 is valid.
 * Byte 0, bit 1 set -> sub 0x01 allowed.
 */
static const uint8_t k_sub_only01[16] = {0x02u, 0u};

/*
 * User service 0xC0: min_len=4, session=extended+programming,
 * security_mask=1, has sub_mask (sub 0x01 only).
 *
 * Used for NRC priority combo tests where multiple gates fail at once.
 * We test which gate the code hits FIRST.
 */
static uds_service_entry_t k_svc_multi[] = {
    {0xC0u, 4u, UDS_SESSION_EXTENDED | UDS_SESSION_PROGRAMMING, 1u, dummy_ok, k_sub_only01, 0u},
};

/* A no-sub service that requires security level 1 and extended+prog sessions */
static uds_service_entry_t k_svc_nosub[] = {
    {0xC1u, 4u, UDS_SESSION_EXTENDED | UDS_SESSION_PROGRAMMING, 1u, dummy_ok, NULL, 0u},
};

/* Helper: set up a UDS context in DEFAULT session, locked security, with the
 * supplied user services. Two will_return() calls are consumed by uds_input_sdu
 * (last_msg_time + p2_timer_start). */
static void send_and_expect_nrc(uds_service_entry_t *svcs, uint16_t svc_count, uint8_t session,
                                uint8_t security_level, const uint8_t *req, uint16_t req_len,
                                uint8_t expected_nrc, uint8_t expected_sid)
{
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.user_services = svcs;
    cfg.user_service_count = svc_count;
    ctx.session.active = session;
    ctx.security.level = security_level;

    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3u);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, req_len);

    assert_int_equal(g_tx_buf[0], 0x7Fu);
    assert_int_equal(g_tx_buf[1], expected_sid);
    assert_int_equal(g_tx_buf[2], expected_nrc);
}

/* ------------------------------------------------------------------ */
/* Fail-closed: a handler returning without setting *out is rejected   */
/* with generalReject (0x10), never a stray positive from tx_buffer.   */
/* ------------------------------------------------------------------ */
static void test_handler_no_result_fails_closed(void **state)
{
    (void) state;
    /* k_svc_noresult (0xC2) is open (session=ALL, no security, min_len=1); the
     * handler runs but leaves *out untouched. execute_handler pre-inits the
     * descriptor to NRC generalReject, so the framework emits 7F C2 10. */
    uint8_t req[] = {0xC2u};
    send_and_expect_nrc(k_svc_noresult, 1u, 0x01u, 0u, req, sizeof(req), 0x10u, 0xC2u);
}

/* ------------------------------------------------------------------ */
/* Phase 2: per-dispatch scratch does not leak into the next request.   */
/* A stale suppress/reset flag set before a fresh top-level request must */
/* be cleared by the scratch reset in uds_input_sdu_addr.               */
/* ------------------------------------------------------------------ */
static void test_scratch_does_not_leak_into_next_request(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    /* Simulate stale per-dispatch scratch left over from a prior request. */
    ctx.scratch.suppress_pos_resp = true;
    ctx.scratch.posttx_kind = (uint8_t) UDS_POSTTX_RESET;
    ctx.scratch.posttx_arg = 0x01u;

    /* A fresh top-level 0x3E (TesterPresent, sub=0x00) must respond normally:
     * the stale suppress must NOT silence it, and the stale post-TX action must
     * NOT survive to trigger the post-emit reset path. */
    uint8_t req[] = {0x3Eu, 0x00u};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2u); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7Eu); /* responded, not suppressed */
    assert_int_equal(g_tx_buf[1], 0x00u);
    /* stale post-TX action was cleared by the scratch reset */
    assert_int_equal(ctx.scratch.posttx_kind, (uint8_t) UDS_POSTTX_NONE);
    assert_false(ctx.scratch.suppress_pos_resp);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(a): wrong-session AND wrong-length              */
/* Session (0x7F) beats length (0x13)                                  */
/* Winning condition: handle_request line ~329 (session before len)    */
/* ------------------------------------------------------------------ */
static void test_nrc_session_beats_length(void **state)
{
    (void) state;
    /* k_svc_nosub requires session=extended|prog AND min_len=4.
     * Send in default session with only 2 bytes -> both session AND length fail.
     * Code checks session FIRST (line 329) before min-length (line 353).
     * Expected: 0x7F (serviceNotSupportedInActiveSession). */
    uint8_t req[] = {0xC1u, 0x00u}; /* too short (2 < 4) AND wrong session */
    /* 0x01 = UDS_SESSION_ID_DEFAULT (active_session stores the session ID, not bitmask) */
    send_and_expect_nrc(k_svc_nosub, 1u, 0x01u, 0u, req, sizeof(req), 0x7Fu, 0xC1u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(b): bad-sub AND wrong-length (len < 2)          */
/* Length (0x13) beats subfunction (0x12) ONLY when len < 2            */
/* Winning condition: handle_request line ~340 (len<2 guard before sub check) */
/* ------------------------------------------------------------------ */
static void test_nrc_length_beats_sub_when_len_lt2(void **state)
{
    (void) state;
    /* k_svc_multi: has sub_mask, min_len=4, session=ext|prog, security=1.
     * In extended session (0x03), locked security. Send 1-byte request (SID only):
     * - sub gate fires: len < 2 -> 0x13 (line 340, BEFORE subfunction check at 346)
     * - session passes (extended), subfunction would be 0x12, min-len would be 0x13,
     *   security would be 0x33.
     * The sub-length guard (line 340) runs first -> 0x13 wins. */
    uint8_t req[] = {0xC0u}; /* only SID byte; len=1 < 2 */
    /* 0x03 = UDS_SESSION_ID_EXTENDED */
    send_and_expect_nrc(k_svc_multi, 1u, 0x03u, 0u, req, sizeof(req), 0x13u, 0xC0u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(b') bad-sub AND wrong-length (len >= 2 but < min_len) */
/* Subfunction (0x12) beats length (0x13) when len >= 2               */
/* Winning condition: handle_request line ~346 (sub check before min-len 353) */
/* ------------------------------------------------------------------ */
static void test_nrc_sub_beats_length_when_len_ge2(void **state)
{
    (void) state;
    /* k_svc_multi: min_len=4, sub_mask only allows sub=0x01.
     * Extended session (0x03), locked security. Send 2 bytes with bad subfunction 0x02:
     * - len=2 >= 2 so sub-length guard at 340 does NOT fire.
     * - Subfunction 0x02 not in mask -> 0x12 (line 346), before min-len (line 353) */
    uint8_t req[] = {0xC0u, 0x02u}; /* bad sub=0x02, len=2 < min_len=4 */
    send_and_expect_nrc(k_svc_multi, 1u, 0x03u, 0u, req, sizeof(req), 0x12u, 0xC0u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(c): wrong-session AND security-not-met          */
/* Session (0x7F) beats security (0x33)                                */
/* Winning condition: handle_request line ~329 (session before security 359) */
/* ------------------------------------------------------------------ */
static void test_nrc_session_beats_security(void **state)
{
    (void) state;
    /* k_svc_nosub requires extended|prog session AND security_mask=1.
     * In DEFAULT session (0x01), locked (level=0): both session AND security fail.
     * Session check (line 329) fires first -> 0x7F. */
    uint8_t req[] = {0xC1u, 0x00u, 0x00u, 0x00u}; /* correct length, bad session+security */
    send_and_expect_nrc(k_svc_nosub, 1u, 0x01u, 0u, req, sizeof(req), 0x7Fu, 0xC1u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(d): bad-sub AND security-not-met                */
/* Subfunction (0x12) beats security (0x33)                            */
/* Winning condition: handle_request line ~346 (sub before security 359) */
/* ------------------------------------------------------------------ */
static void test_nrc_sub_beats_security(void **state)
{
    (void) state;
    /* k_svc_multi: sub_mask only allows sub=0x01, security_mask=1.
     * Extended session (0x03), locked. Send 4 bytes with bad sub=0x03:
     * - Session passes.
     * - Sub check (line 346): sub=0x03 not in mask -> 0x12, before security (line 359). */
    uint8_t req[] = {0xC0u, 0x03u, 0x00u, 0x00u}; /* bad sub=0x03, locked security */
    send_and_expect_nrc(k_svc_multi, 1u, 0x03u, 0u, req, sizeof(req), 0x12u, 0xC0u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(e): wrong-session AND bad-sub                   */
/* Session (0x7F) beats subfunction (0x12)                             */
/* Winning condition: handle_request line ~329 (session before sub 346) */
/* ------------------------------------------------------------------ */
static void test_nrc_session_beats_sub(void **state)
{
    (void) state;
    /* k_svc_multi: session=ext|prog, sub only 0x01.
     * DEFAULT session (0x01), send bad sub=0x02, correct length:
     * - Session fails (line 329) -> 0x7F, BEFORE subfunction check (line 346). */
    uint8_t req[] = {0xC0u, 0x02u, 0x00u, 0x00u};
    send_and_expect_nrc(k_svc_multi, 1u, 0x01u, 0u, req, sizeof(req), 0x7Fu, 0xC0u);
}

/* ------------------------------------------------------------------ */
/* NRC priority item 1(f): min-len AND security-not-met (no sub_mask)  */
/* Length (0x13) beats security (0x33)                                  */
/* Winning condition: handle_request line ~353 (min-len before security 359) */
/* ------------------------------------------------------------------ */
static void test_nrc_length_beats_security(void **state)
{
    (void) state;
    /* k_svc_nosub: min_len=4, security_mask=1, no sub_mask.
     * Extended session (0x03), locked. Send 2 bytes:
     * - Session: extended passes.
     * - No sub_mask: sub block skipped.
     * - Min-len (line 353): 2 < 4 -> 0x13, before security (line 359). */
    uint8_t req[] = {0xC1u, 0x00u}; /* short */
    send_and_expect_nrc(k_svc_nosub, 1u, 0x03u, 0u, req, sizeof(req), 0x13u, 0xC1u);
}

/* ------------------------------------------------------------------ */
/* Functional addressing 2(a): unknown SID -> silently dropped         */
/* Locking the code path at uds_send_nrc() line ~818 which suppresses  */
/* 0x11 on functional. Verified by test_addressing_dispatch.c #9 but   */
/* that test uses user-defined SIDs. This test uses a core SID range.  */
/* SKIP: already comprehensively covered by                            */
/*   test_addressing_dispatch.c::test_suppress_unknown_sid (0xBF ->    */
/*   functional silent, physical 0x11). No new combination here.       */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Functional addressing 2(b): 0x3E with suppressPosRsp on functional */
/* -> no response at all (subfunction suppress + functional silence)   */
/* SKIP: covered by test_compliance_suppress.c::test_suppress_tester_present
 * (physical). The functional path is implicitly the same: suppress bit
 * prevents positive, and even if it weren't suppressed, functional +
 * 0x3E would still succeed silently only if it somehow needed NRC. The
 * physical-suppress test is the definitive lock; no distinct functional
 * variant is needed.                                                   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Functional addressing 2(c): 0x31 RoutineControl functional          */
/* ISO says functional 0x31 that returns NRC 0x12/0x7E/0x11/0x31 must */
/* be suppressed. Already covered by                                    */
/*   test_addressing_dispatch.c::test_suppress_0x12_functional and     */
/*   test_compliance_suppress.c::test_suppress_routine_control.        */
/* SKIP: no new combination.                                            */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 0x36 TransferData item 3: first block != 0x01 -> 0x24               */
/* Already in test_service_flash.c::test_transfer_data_sequence_error  */
/* SKIP: exact path covered.                                            */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 0x36 TransferData item 3': correct 0x01 -> 0x02 increment accepted  */
/* SKIP: covered by test_service_flash.c::test_transfer_data_sequence_error
 * (the first-block seq=0x02 rejected case implicitly proves seq=0x01   */
/* accepted via the success path in test_transfer_data_last_block_replay) */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 0x36: flash_sequence mismatch mid-transfer -> 0x24                  */
/* Not the same as first-block wrong: tests the else branch (line 125  */
/* of uds_service_flash.c) where flash_sequence != 0 and sequence !=   */
/* expected. test_service_flash does seq=0, seq=0x02 (first-block case).*/
/* This tests seq=0x01 established, then seq=0x03 sent -> mismatch.    */
/* ------------------------------------------------------------------ */
static int mock_transfer_data_a3(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data,
                                 uint16_t len)
{
    (void) ctx;
    (void) sequence;
    (void) data;
    (void) len;
    return 0;
}

static void test_36_mid_transfer_seq_mismatch(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data_a3;

    /* Arm the transfer so 0x36 is accepted (develop conformance: transfer_active
     * must be true before TransferData, otherwise NRC 0x24). */
    ctx.server.transfer_active = true;

    /* Step 1: Send block 0x01 successfully to establish flash_sequence=1 */
    uint8_t req1[] = {0x36u, 0x01u, 0xAAu, 0xBBu};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2u); /* 76 01 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req1, sizeof(req1));
    assert_int_equal(ctx.server.flash_sequence, 0x01u);

    /* Step 2: Send block 0x03 (expected 0x02) while transfer is active ->
     * NRC 0x73 (wrongBlockSequenceCounter).
     * Develop conformance: "no transfer" -> 0x24; active but wrong counter -> 0x73. */
    uint8_t req2[] = {0x36u, 0x03u, 0xAAu, 0xBBu};
    will_return(mock_get_time, 2000u);
    will_return(mock_get_time, 2000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3u); /* 7F 36 73 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req2, sizeof(req2));
    assert_int_equal(g_tx_buf[0], 0x7Fu);
    assert_int_equal(g_tx_buf[1], 0x36u);
    assert_int_equal(g_tx_buf[2], 0x73u); /* wrongBlockSequenceCounter */
}

/* ------------------------------------------------------------------ */
/* 0x36: wrap-around 0xFF -> 0x00 accepted                             */
/* Locking: uds_service_flash.c line ~126 (wrap: 0xFF -> 0x00 = expected) */
/* ------------------------------------------------------------------ */
static void test_36_sequence_wrap_ff_to_00(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data_a3;

    /* Directly set flash_sequence to 0xFF to simulate being at the wrap point.
     * Arm the transfer (develop conformance). */
    ctx.server.flash_sequence = 0xFFu;
    ctx.server.transfer_active = true;

    /* Send block 0x00 (expected after 0xFF wrap) -> positive response */
    uint8_t req[] = {0x36u, 0x00u, 0xCCu, 0xDDu};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2u); /* 76 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[0], 0x76u); /* positive response */
    assert_int_equal(g_tx_buf[1], 0x00u);
    assert_int_equal(ctx.server.flash_sequence, 0x00u);
}

/* ------------------------------------------------------------------ */
/* 0x36: armed transfer, wrap-around wrong block -> NRC 0x73           */
/* After flash_sequence=0xFF the expected counter is 0x00; sending 0x01 */
/* with transfer_active is wrongBlockSequenceCounter (0x73).           */
/* ------------------------------------------------------------------ */
static void test_36_sequence_wrap_mismatch(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data_a3;

    /* Arm the transfer (develop conformance). */
    ctx.server.flash_sequence = 0xFFu;
    ctx.server.transfer_active = true;

    /* Expected is 0x00 (wrap), but sent 0x01 -> active transfer, wrong counter
     * -> NRC 0x73 (wrongBlockSequenceCounter), not 0x24. */
    uint8_t req[] = {0x36u, 0x01u, 0xCCu, 0xDDu}; /* expected 0x00, got 0x01 */
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3u); /* 7F 36 73 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[0], 0x7Fu);
    assert_int_equal(g_tx_buf[1], 0x36u);
    assert_int_equal(g_tx_buf[2], 0x73u); /* wrongBlockSequenceCounter */
}

/* ------------------------------------------------------------------ */
/* 0x84 item 4(a): inner request returns PENDING                        */
/*                                                                      */
/* When the inner handler calls uds_pending(), execute_handler() at    */
/* line ~257-262 sends NRC 0x78 via uds_send_nrc(). uds_send_nrc      */
/* detects ctx->scratch.secure_capturing == true and stores the 3-byte NRC     */
/* into the capture buffer (line ~831). So captured_len = 3 (a NRC    */
/* frame). fn_secure_encode is then called on it, and the outer 0x84   */
/* response carries the encoded NRC 0x78 frame.                         */
/*                                                                      */
/* This is NOT a stop-and-report: the behavior is architecturally       */
/* consistent — 0x84 treats the inner NRC 0x78 like any inner NRC and  */
/* secures it. The outer client sees C4 <APAR> <encrypted NRC 0x78>.   */
/* ------------------------------------------------------------------ */
static void inner_pending_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                  uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_pending(out);
}

/* XOR 0xFF decode, XOR 0xAA encode (same reversible test crypto as test_service_84.c) */
static int a3_xor_decode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                         uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    assert_true(in_len <= out_max);
    for (uint16_t i = 0u; i < in_len; i++) {
        out[i] = (uint8_t) (in[i] ^ 0xFFu);
    }
    return (int) in_len;
}

static int a3_xor_encode(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                         uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    assert_true(in_len <= out_max);
    for (uint16_t i = 0u; i < in_len; i++) {
        out[i] = (uint8_t) (in[i] ^ 0xAAu);
    }
    return (int) in_len;
}

static const uds_service_entry_t k_pending_svc[] = {
    {0xD0u, 1u, UDS_SESSION_SECURED, 0u, inner_pending_handler, NULL, 0u},
};

static void test_84_inner_pending_yields_secured_nrc78(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_pending_svc;
    cfg.user_service_count = 1u;
    cfg.fn_secure_decode = a3_xor_decode;
    cfg.fn_secure_encode = a3_xor_encode;

    /* inner = {0xD0}; secured payload = {0xD0 ^ 0xFF} = {0x2F} */
    uint8_t req[] = {0x84u, 0x00u, 0x01u, 0x2Fu};

    /* uds_input_sdu consumes 2 mock_get_time calls:
     *   (1) last_msg_time = get_time_ms()
     *   (2) p2_timer_start = get_time_ms()
     * The inner PENDING path (execute_handler line ~261) adds a 3rd:
     *   (3) p2_timer_start = get_time_ms()  (sets pending state on inner dispatch).
     *
     * After the inner dispatch, 0x84 finds captured_len=3 (the 7F D0 78 NRC frame),
     * calls fn_secure_encode on it, and emits the outer C4 response. */
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u); /* inner PENDING: p2_timer_start */
    /* The inner handler calls uds_pending() -> execute_handler sends NRC 0x78.
     * uds_send_nrc captures it into secure_capture_buf (3 bytes: 7F D0 78).
     * captured_len=3 (not 0) -> fn_secure_encode is called on those 3 bytes.
     * Outer response: C4 00 01 + encode({7F D0 78} ^ 0xAA) = {D5 7A D2}.
     * Total outer response = 6 bytes. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6u); /* C4 APAR(2) + 3 encrypted bytes */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));

    /* Verify outer SID and APAR */
    assert_int_equal(g_tx_buf[0], 0xC4u); /* 0x84 + 0x40 */
    assert_int_equal(g_tx_buf[1], 0x00u);
    assert_int_equal(g_tx_buf[2], 0x01u);
    /* Verify the encrypted inner NRC: {7F D0 78} ^ 0xAA = {D5 7A D2} */
    assert_int_equal(g_tx_buf[3], (uint8_t) (0x7Fu ^ 0xAAu)); /* D5 */
    assert_int_equal(g_tx_buf[4], (uint8_t) (0xD0u ^ 0xAAu)); /* 7A */
    assert_int_equal(g_tx_buf[5], (uint8_t) (0x78u ^ 0xAAu)); /* D2 */

    /* Safety invariant: the inner-PENDING state set on the inner dispatch
     * (execute_handler line ~261) MUST be cleared by uds_emit_response on the
     * outer positive path. If a future change broke that cleanup, p2_msg_pending
     * would leak into the next request (the #80 bug class). Lock it. */
    assert_false(ctx.server.p2_msg_pending);
    assert_int_equal(ctx.server.pending_sid, 0u);
}

/* ------------------------------------------------------------------ */
/* 0x84 item 4(b): inner response too long -> NRC 0x14                 */
/* SKIP: already covered by                                             */
/*   test_service_84.c::test_secured_inner_response_too_long (257-byte */
/*   inner response exceeds UDS_SECURE_SCRATCH=256). No new combination.*/
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 0x84 item 4(c): inner request yields an NRC -> 0x84 surfaces it    */
/* This is distinct from the no-hook (0x22) and bad-MAC (0x33) tests.  */
/* Tests a valid decode, inner service returns NRC 0x22 directly.      */
/* ------------------------------------------------------------------ */
static void inner_nrc22_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_nrc(out, 0x22u); /* conditionsNotCorrect */
}

static const uds_service_entry_t k_inner_nrc22_svc[] = {
    {0xD1u, 1u, UDS_SESSION_SECURED, 0u, inner_nrc22_handler, NULL, 0u},
};

static void test_84_inner_nrc_surfaced_encrypted(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_inner_nrc22_svc;
    cfg.user_service_count = 1u;
    cfg.fn_secure_decode = a3_xor_decode;
    cfg.fn_secure_encode = a3_xor_encode;

    /* inner = {0xD1}; secured payload = {0xD1 ^ 0xFF} = {0x2E} */
    uint8_t req[] = {0x84u, 0x12u, 0x34u, 0x2Eu};

    /* Inner handler returns NRC 0x22 via uds_nrc() -> execute_handler calls
     * uds_send_nrc(ctx, 0xD1, 0x22) -> captured as {7F D1 22} (3 bytes).
     * fn_secure_encode encrypts {7F D1 22} ^ 0xAA = {D5 7B 88}.
     * Outer response: C4 12 34 D5 7B 88 = 6 bytes. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6u);
    will_return(mock_tp_send, 0);

    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);

    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0xC4u);
    assert_int_equal(g_tx_buf[1], 0x12u);
    assert_int_equal(g_tx_buf[2], 0x34u);
    /* Encrypted inner NRC: {7F D1 22} ^ 0xAA */
    assert_int_equal(g_tx_buf[3], (uint8_t) (0x7Fu ^ 0xAAu)); /* D5 */
    assert_int_equal(g_tx_buf[4], (uint8_t) (0xD1u ^ 0xAAu)); /* 7B */
    assert_int_equal(g_tx_buf[5], (uint8_t) (0x22u ^ 0xAAu)); /* 88 */
}

/* ------------------------------------------------------------------ */
/* Security gating order item 5: DID requires security; session also   */
/* wrong -> session (0x7F) wins over security (0x33).                  */
/* SKIP: covered by test_nrc_session_beats_security above for the      */
/* generic service case. A DID-specific variant adds no new code path  */
/* because the DID security gate is inside the handler (post-dispatch) */
/* not in handle_request. The session gate in handle_request is the    */
/* only relevant level here, already locked above.                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Safety hook vs security: fn_is_safe=false AND security not met      */
/* Security (0x33) beats safety (0x22): line ~359 before line ~370.   */
/* ------------------------------------------------------------------ */
static bool always_unsafe(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) sid;
    (void) data;
    (void) len;
    return false;
}

static void test_nrc_security_beats_safety(void **state)
{
    (void) state;
    /* k_svc_nosub: security_mask=1.  fn_is_safe=always_unsafe.
     * Extended session, locked security.  Both security and safety fail.
     * Security check (line 359) fires first -> 0x33. */
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.user_services = k_svc_nosub;
    cfg.user_service_count = 1u;
    cfg.fn_is_safe = always_unsafe;
    ctx.session.active = 0x03u; /* UDS_SESSION_ID_EXTENDED */
    ctx.security.level = 0u;    /* locked */

    uint8_t req[] = {0xC1u, 0x00u, 0x00u, 0x00u};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3u);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7Fu);
    assert_int_equal(g_tx_buf[1], 0xC1u);
    assert_int_equal(g_tx_buf[2], 0x33u); /* security beats safety */
}

/* ------------------------------------------------------------------ */
/* Functional 0x3E + suppressPosRsp + functional addressing:           */
/* functional addressing + suppress bit -> absolutely no frame.        */
/* DISTINCT from test_compliance_suppress which uses physical.         */
/* ------------------------------------------------------------------ */
static void test_functional_3E_suppress_silent(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* 0x3E sub=0x00 | 0x80 (suppress bit) sent via functional addressing.
     * Two outcomes both yield silence:
     *   (1) suppress bit -> no positive response emitted.
     *   (2) functional addressing -> even if NRC would be generated, suppressed NRCs
     *       (0x12/0x7E etc.) would be silenced.
     * Net: no frame. Registers 2 will_return for time, no mock_tp_send expectation. */
    uint8_t req[] = {0x3Eu, 0x80u}; /* TesterPresent sub=0x00 | suppress */
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    /* No expect_*() for mock_tp_send: any send is a test failure. */
    uds_input_sdu_addr(&ctx, req, sizeof(req), UDS_ADDR_FUNCTIONAL);
}

/* ------------------------------------------------------------------ */
/* Functional 0x7F suppressed: serviceNotSupportedInActiveSession is   */
/* suppressed on functional. Distinct from 0x11 (unknown SID) test in  */
/* test_addressing_dispatch. Session-gated service + functional.        */
/* ------------------------------------------------------------------ */
static void test_functional_0x7F_suppressed(void **state)
{
    (void) state;
    /* k_svc_nosub needs extended|prog session. Send in DEFAULT session via
     * functional addressing -> 0x7F would be generated but must be silenced
     * (uds_send_nrc line ~818 suppresses 0x7F on functional). */
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.user_services = k_svc_nosub;
    cfg.user_service_count = 1u;
    ctx.session.active = 0x01u; /* UDS_SESSION_ID_DEFAULT */

    uint8_t req[] = {0xC1u, 0x00u, 0x00u, 0x00u};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    /* No mock_tp_send expectation: 0x7F must be suppressed on functional. */
    uds_input_sdu_addr(&ctx, req, sizeof(req), UDS_ADDR_FUNCTIONAL);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* NRC priority combos */
        cmocka_unit_test(test_handler_no_result_fails_closed),
        cmocka_unit_test(test_scratch_does_not_leak_into_next_request),
        cmocka_unit_test(test_nrc_session_beats_length),
        cmocka_unit_test(test_nrc_length_beats_sub_when_len_lt2),
        cmocka_unit_test(test_nrc_sub_beats_length_when_len_ge2),
        cmocka_unit_test(test_nrc_session_beats_security),
        cmocka_unit_test(test_nrc_sub_beats_security),
        cmocka_unit_test(test_nrc_session_beats_sub),
        cmocka_unit_test(test_nrc_length_beats_security),
        cmocka_unit_test(test_nrc_security_beats_safety),
        /* 0x36 TransferData */
        cmocka_unit_test(test_36_mid_transfer_seq_mismatch),
        cmocka_unit_test(test_36_sequence_wrap_ff_to_00),
        cmocka_unit_test(test_36_sequence_wrap_mismatch),
        /* 0x84 SecuredDataTransmission */
        cmocka_unit_test(test_84_inner_pending_yields_secured_nrc78),
        cmocka_unit_test(test_84_inner_nrc_surfaced_encrypted),
        /* Functional addressing */
        cmocka_unit_test(test_functional_3E_suppress_silent),
        cmocka_unit_test(test_functional_0x7F_suppressed),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
