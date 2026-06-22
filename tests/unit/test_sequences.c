/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_sequences.c
 * @brief Multi-service sequence regression tests (stateful diagnostic flows).
 */

#include <string.h>

#include "test_helpers.h"

/* ------------------------------------------------------------------ */
/* Shared helpers                                                       */
/* ------------------------------------------------------------------ */

static int mock_security_seed(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf,
                              uint16_t max_len)
{
    (void) ctx;
    (void) level;
    (void) max_len;
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

static int mock_security_key(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                             const uint8_t *key, uint16_t key_len)
{
    (void) ctx;
    (void) level;
    (void) seed;
    (void) key_len;
    if (key[0] == 0xDF && key[1] == 0xAE && key[2] == 0xBF && key[3] == 0xF0) return 0;
    return -0x35;
}

/* Perform a full seed+key unlock on ctx (fn_security_seed/key must be set). */
static void do_unlock(uds_ctx_t *ctx)
{
    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 67 01 DE AD BE EF */
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, seed_req, sizeof(seed_req));

    uint8_t key_req[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 67 02 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(ctx, key_req, sizeof(key_req));
}

/* DID requiring security level 1.
 * security_mask=2 means bit-1 set: allowed when (1u << security_level) & 2 != 0,
 * i.e. security_level == 1.  At level 0 the check yields 0 -> denied (NRC 0x33). */
static int my_did_read(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    (void) max_len;
    buf[0] = 0xAB;
    return 1;
}

static const uds_did_entry_t k_secured_dids[] = {
    {0x1234u, 1u, 0u, 2u, my_did_read, NULL, NULL},
};

static uds_did_table_t k_secured_did_table = {k_secured_dids, 1u};

/* ------------------------------------------------------------------ */
/* Test 1: security blocked -> unlock -> allowed                        */
/* ------------------------------------------------------------------ */
static void test_seq_security_blocked_unlock_allowed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;
    cfg.did_table = k_secured_did_table;

    /* 1a. Request secured DID while locked -> NRC 0x33 */
    uint8_t rdbi_req[] = {0x22, 0x12, 0x34};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 22 33 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, rdbi_req, sizeof(rdbi_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x33);

    /* 1b. Seed + key unlock */
    do_unlock(&ctx);
    assert_int_equal(ctx.security_level, 1);

    /* 1c. Request secured DID while unlocked -> positive response */
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 62 12 34 AB */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, rdbi_req, sizeof(rdbi_req));
    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0x12);
    assert_int_equal(g_tx_buf[2], 0x34);
    assert_int_equal(g_tx_buf[3], 0xAB);
}

/* ------------------------------------------------------------------ */
/* Test 2: session change re-locks + downstream denied                  */
/* ------------------------------------------------------------------ */
static void test_seq_session_change_relocks(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;
    cfg.did_table = k_secured_did_table;

    /* Enter extended session */
    uint8_t ext_req[] = {0x10, 0x03};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, ext_req, sizeof(ext_req));

    /* Unlock */
    do_unlock(&ctx);
    assert_int_equal(ctx.security_level, 1);

    /* Switch to programming session -> security_level cleared */
    uint8_t prog_req[] = {0x10, 0x02};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, prog_req, sizeof(prog_req));
    assert_int_equal(ctx.security_level, 0);

    /* Request secured DID -> NRC 0x33 */
    uint8_t rdbi_req[] = {0x22, 0x12, 0x34};
    will_return(mock_get_time, 3000);
    will_return(mock_get_time, 3000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 22 33 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, rdbi_req, sizeof(rdbi_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[2], 0x33);
}

/* ------------------------------------------------------------------ */
/* Test 3: S3 timeout re-locks + denied                                 */
/* ------------------------------------------------------------------ */
static void test_seq_s3_timeout_relocks(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;
    cfg.did_table = k_secured_did_table;

    /* Enter extended session at T=0 */
    uint8_t ext_req[] = {0x10, 0x03};
    will_return(mock_get_time, 0);
    will_return(mock_get_time, 0);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, ext_req, sizeof(ext_req));

    /* Unlock at T=0 */
    do_unlock(&ctx);
    assert_int_equal(ctx.security_level, 1);

    /* Advance clock past S3 (5000 ms) and fire uds_process */
    will_return(mock_get_time, 6001u);
    uds_process(&ctx);
    /* S3 fires: session resets to default, security_level cleared */
    assert_int_equal(ctx.security_level, 0);

    /* Request secured DID -> NRC 0x33 */
    uint8_t rdbi_req[] = {0x22, 0x12, 0x34};
    will_return(mock_get_time, 7000u);
    will_return(mock_get_time, 7000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 22 33 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, rdbi_req, sizeof(rdbi_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[2], 0x33);
}

/* ------------------------------------------------------------------ */
/* Test 4: seed -> session-change -> key = NRC 0x24                     */
/* ------------------------------------------------------------------ */
static void test_seq_seed_session_key_is_0x24(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;

    /* 4a. Request seed */
    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));
    assert_int_not_equal(ctx.security_seed_level, 0);

    /* 4b. Session change: clears security_seed_level */
    uint8_t sess_req[] = {0x10, 0x01};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, sess_req, sizeof(sess_req));
    assert_int_equal(ctx.security_seed_level, 0);

    /* 4c. Send key -> NRC 0x24 (requestSequenceError: no pending seed) */
    uint8_t key_req[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};
    will_return(mock_get_time, 3000);
    will_return(mock_get_time, 3000);
    will_return(mock_get_time, 3000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 24 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, key_req, sizeof(key_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x27);
    assert_int_equal(g_tx_buf[2], 0x24);
}

/* ------------------------------------------------------------------ */
/* Test 5: RCRRP recovery — next service responds normally              */
/* ------------------------------------------------------------------ */
static void async_pending_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                  uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_pending(out);
}

static const uds_service_entry_t k_async_svcs[] = {
    {0x31u, 4u, UDS_SESSION_ALL, 0u, async_pending_handler, NULL, 0u},
};

static void test_seq_rcrrp_recovery(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_async_svcs;
    cfg.user_service_count = 1u;

    /* 5a. Send 31 01 FF 00 -> handler returns PENDING -> 0x78.
     * Three time calls: last_msg_time, p2_timer_start, p2_timer_start-on-pending. */
    uint8_t req[] = {0x31, 0x01, 0xFF, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 31 78 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_true(ctx.p2_msg_pending);

    /* 5b. Complete the async response */
    ctx.config->tx_buffer[0] = 0x71;
    ctx.config->tx_buffer[1] = 0x01;
    ctx.config->tx_buffer[2] = 0xFF;
    ctx.config->tx_buffer[3] = 0x00;
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4);
    will_return(mock_tp_send, 0);
    uds_send_response(&ctx, 4);
    assert_false(ctx.p2_msg_pending);

    /* 5c. Send 3E 00 -> must get 7E 00 (NOT NRC 0x21 busy) */
    uint8_t tp_req[] = {0x3E, 0x00};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, tp_req, sizeof(tp_req));
    assert_int_equal(g_tx_buf[0], 0x7E);
    assert_int_equal(g_tx_buf[1], 0x00);
}

/* ------------------------------------------------------------------ */
/* Test 6: NVM warm-start gated service works                           */
/* ------------------------------------------------------------------ */
static uint8_t g_nvm_storage_seq[2] = {0x03u, 0x01u}; /* extended session, level=1 */

static int seq_nvm_load(struct uds_ctx *ctx, uint8_t *state, uint16_t len)
{
    (void) ctx;
    if (len >= 2u) {
        state[0] = g_nvm_storage_seq[0];
        state[1] = g_nvm_storage_seq[1];
        return 2;
    }
    return -1;
}

static void test_seq_nvm_warmstart_gated_works(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;

    memset(&cfg, 0, sizeof(uds_config_t));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.fn_nvm_load = seq_nvm_load;
    cfg.did_table = k_secured_did_table;

    uds_init(&ctx, &cfg);

    /* NVM restored: session=0x03, level=1 */
    assert_int_equal(ctx.active_session, 0x03);
    assert_int_equal(ctx.security_level, 1);

    /* Request secured DID -> positive response (level=1 satisfies security_mask=2u) */
    uint8_t rdbi_req[] = {0x22, 0x12, 0x34};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 62 12 34 AB */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, rdbi_req, sizeof(rdbi_req));
    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[3], 0xAB);
}

/* ------------------------------------------------------------------ */
/* Test 7: auth deauth -> gated -> NRC 0x34                             */
/* ------------------------------------------------------------------ */
static void auth_gated_svc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    uds_ok(out, 1u);
}

static const uds_service_entry_t k_auth_svcs[] = {
    {0xCCu, 1u, UDS_SESSION_ALL, 0u, auth_gated_svc, NULL, 0u},
};

static bool gate_0xCC(uds_ctx_t *ctx, uint8_t sid)
{
    (void) ctx;
    return (sid == 0xCCu);
}

static void test_seq_auth_deauth_gated_denied(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_auth_svcs;
    cfg.user_service_count = 1u;
    cfg.fn_auth_required = gate_0xCC;
    ctx.authenticated = true;

    /* 7a. deAuthenticate (0x29 0x00) -> 69 00 10 */
    uint8_t deauth_req[] = {0x29, 0x00};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 69 00 10 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, deauth_req, sizeof(deauth_req));
    assert_false(ctx.authenticated);

    /* 7b. Request gated service -> NRC 0x34 (authenticationRequired) */
    uint8_t svc_req[] = {0xCC};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F CC 34 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, svc_req, sizeof(svc_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0xCC);
    assert_int_equal(g_tx_buf[2], 0x34);
}

/* ------------------------------------------------------------------ */
/* Test 8a: suppressPosRsp generalized — 0x85 ControlDTCSetting        */
/* ------------------------------------------------------------------ */
static void test_seq_suppress_pos_rsp_85(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* 0x85 subfn=0x02 (disableDTCSetting) | bit7 = 0x82 -> suppress */
    uint8_t ctrl_dtc_req[] = {0x85, 0x82};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No mock_tp_send expectation: must not emit */
    uds_input_sdu(&ctx, ctrl_dtc_req, sizeof(ctrl_dtc_req));

    /* Next service (3E 00) must respond normally */
    uint8_t tp_req[] = {0x3E, 0x00};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, tp_req, sizeof(tp_req));
    assert_int_equal(g_tx_buf[0], 0x7E);
    assert_int_equal(g_tx_buf[1], 0x00);
}

/* ------------------------------------------------------------------ */
/* Test 8b: suppressPosRsp generalized — 0x2A ReadDataByPeriodicId     */
/* ------------------------------------------------------------------ */
static int mock_periodic_read_seq(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                                  uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    if (periodic_id == 0xE1u) {
        out_buf[0] = 0x11u;
        out_buf[1] = 0x22u;
        return 2;
    }
    return -1;
}

static void test_seq_suppress_pos_rsp_2A(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_periodic_read = mock_periodic_read_seq;

    /* 0x2A subfn=0x01 (fast rate) | bit7 = 0x81 -> suppress */
    uint8_t per_req[] = {0x2A, 0x81, 0xE1};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No mock_tp_send expectation: must not emit */
    uds_input_sdu(&ctx, per_req, sizeof(per_req));
    assert_int_equal(ctx.periodic_count, 1u); /* subscription registered despite suppress */

    /* Next service (3E 00) must respond normally */
    uint8_t tp_req[] = {0x3E, 0x00};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, tp_req, sizeof(tp_req));
    assert_int_equal(g_tx_buf[0], 0x7E);
    assert_int_equal(g_tx_buf[1], 0x00);
}

/* ------------------------------------------------------------------ */
/* Test 8c: suppressPosRsp generalized — 0x19 ReadDTCInformation        */
/* ------------------------------------------------------------------ */
static int mock_dtc_read_seq(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req,
                             uint16_t req_len, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) subfn;
    (void) req;
    (void) req_len;
    (void) max_len;
    out_buf[0] = 0xAA;
    return 1;
}

static void test_seq_suppress_pos_rsp_19(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read_seq;

    /* 0x19 subfn=0x01 (reportNumberOfDTCByStatusMask) | bit7 = 0x81 -> suppress */
    uint8_t dtc_req[] = {0x19, 0x81, 0xFF};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No mock_tp_send expectation: must not emit */
    uds_input_sdu(&ctx, dtc_req, sizeof(dtc_req));

    /* Next service (3E 00) must respond normally */
    uint8_t tp_req[] = {0x3E, 0x00};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, tp_req, sizeof(tp_req));
    assert_int_equal(g_tx_buf[0], 0x7E);
    assert_int_equal(g_tx_buf[1], 0x00);
}

/* ------------------------------------------------------------------ */
/* Test 9: cross-feature stale-flag (0x84 overflow then ROE emit)       */
/* ------------------------------------------------------------------ */

/* Inner secured-session service that returns 257 bytes -> overflow */
static void large_inner_svc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) data;
    (void) len;
    memset(ctx->config->tx_buffer, 0xABu, 257u);
    uds_ok(out, 257u);
}

static const uds_service_entry_t k_large_inner_svc[] = {
    {0xBBu, 1u, UDS_SESSION_SECURED, 0u, large_inner_svc, NULL, 0u},
};

static int xor_decode_seq(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                          uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) out_max;
    for (uint16_t i = 0u; i < in_len; i++) out[i] = (uint8_t) (in[i] ^ 0xFFu);
    return (int) in_len;
}

static int xor_encode_seq(uds_ctx_t *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                          uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) out_max;
    for (uint16_t i = 0u; i < in_len; i++) out[i] = (uint8_t) (in[i] ^ 0xAAu);
    return (int) in_len;
}

static int mock_did_read_roe(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) did;
    (void) max_len;
    buf[0] = 0xDE;
    buf[1] = 0xAD;
    return 2;
}

static const uds_did_entry_t k_roe_dids[] = {
    {0xF190u, 2u, 0u, 0u, mock_did_read_roe, NULL, NULL},
};

static uds_did_table_t k_roe_did_table = {k_roe_dids, 1u};

static void test_seq_84_overflow_then_roe(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.user_services = k_large_inner_svc;
    cfg.user_service_count = 1u;
    cfg.fn_secure_decode = xor_decode_seq;
    cfg.fn_secure_encode = xor_encode_seq;
    cfg.did_table = k_roe_did_table;

    /* Step 1: ROE setup onChangeOfDataIdentifier(0xF190) -> STR = 22 F1 90 */
    uint8_t roe_setup[] = {0x86, 0x03, 0x02, 0xF1, 0x90, 0x22, 0xF1, 0x90};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9); /* C6 03 <count=1> + echo(6) = 9 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, roe_setup, sizeof(roe_setup));

    uint8_t roe_start[] = {0x86, 0x05};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, roe_start, sizeof(roe_start));

    /* Step 2: 0x84 with inner=0xBB (257-byte response) -> NRC 0x14 (responseTooLong)
     * inner = {0xBB, 0x01}; secured payload = inner ^ 0xFF = {0x44, 0xFE} */
    uint8_t sec_req[] = {0x84, 0x12, 0x34, 0x44, 0xFE};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 84 14 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, sec_req, sizeof(sec_req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[2], 0x14);

    /* Step 3: ROE trigger -> inner 22 F1 90 -> 62 F1 90 DE AD (5 bytes) fits.
     * secure_capture_overflow must have been reset; ROE must emit.
     * C6 03 <numIdentified=1> + inner(5) = 8 bytes. */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 8);
    will_return(mock_tp_send, 0);
    int emitted = uds_roe_trigger(&ctx, 0x03u, 0xF190u);
    assert_int_equal(emitted, 1);
    assert_int_equal(g_tx_buf[0], 0xC6);
    assert_int_equal(g_tx_buf[1], 0x03);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_seq_security_blocked_unlock_allowed),
        cmocka_unit_test(test_seq_session_change_relocks),
        cmocka_unit_test(test_seq_s3_timeout_relocks),
        cmocka_unit_test(test_seq_seed_session_key_is_0x24),
        cmocka_unit_test(test_seq_rcrrp_recovery),
        cmocka_unit_test(test_seq_nvm_warmstart_gated_works),
        cmocka_unit_test(test_seq_auth_deauth_gated_denied),
        cmocka_unit_test(test_seq_suppress_pos_rsp_85),
        cmocka_unit_test(test_seq_suppress_pos_rsp_2A),
        cmocka_unit_test(test_seq_suppress_pos_rsp_19),
        cmocka_unit_test(test_seq_84_overflow_then_roe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
