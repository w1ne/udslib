/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_core_coverage.c
 * @brief Regression coverage for uds_core.c paths not exercised elsewhere:
 *        logging filter, uds_client_request, uds_init validation/NVM/strict,
 *        uds_process (mutex, S3, P2*, periodic), secured-data error paths,
 *        and the unknown-session-bit default.
 */

#include "test_helpers.h"
#include "uds_internal.h"

/* --- A non-mock time source so uds_process tests need no will_return() --- */
static uint32_t g_time;
static uint32_t fixed_time(void)
{
    return g_time;
}

/* --- A non-strict send sink so uds_process/periodic tests can observe sends
 *     without cmocka expect bookkeeping. --- */
static int g_send_calls;
static uint16_t g_last_send_len;
static uint8_t g_last_send[64];
static int counting_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_send_calls++;
    g_last_send_len = len;
    if (len <= sizeof(g_last_send)) {
        memcpy(g_last_send, data, len);
    }
    return 0;
}

/* ---- Logging ---- */

static int g_log_calls;
static uint8_t g_last_log_level;
static void counting_log(uint8_t level, const char *msg)
{
    (void) msg;
    g_log_calls++;
    g_last_log_level = level;
}

/* fn_log is invoked only when level <= log_level (uds_core.c:110-111). */
static void test_log_emitted_when_level_allowed(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_log = counting_log;
    cfg.log_level = UDS_LOG_INFO;

    g_log_calls = 0;
    /* uds_init logs "UDS Stack Initialized" at INFO. */
    assert_int_equal(uds_init(&ctx, &cfg), UDS_OK);
    assert_true(g_log_calls >= 1);
    assert_int_equal(g_last_log_level, UDS_LOG_INFO);
}

/* fn_log is suppressed when level > log_level (only the false branch of :110). */
static void test_log_suppressed_when_level_too_high(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_log = counting_log;
    cfg.log_level = UDS_LOG_ERROR; /* INFO (1) > ERROR (0): suppressed */

    g_log_calls = 0;
    assert_int_equal(uds_init(&ctx, &cfg), UDS_OK);
    assert_int_equal(g_log_calls, 0);
}

/* ---- uds_client_request (uds_core.c:640-674) ---- */

static int g_cb_calls;
static void client_cb(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) sid;
    (void) data;
    (void) len;
    g_cb_calls++;
}

static void test_client_request_not_init(void **state)
{
    (void) state;
    uint8_t payload[] = {0x01};
    /* NULL ctx -> NOT_INIT. */
    assert_int_equal(uds_client_request(NULL, 0x10, payload, 1u, NULL), UDS_ERR_NOT_INIT);

    /* ctx with NULL tx_buffer -> NOT_INIT. */
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ctx.config = &cfg; /* cfg.tx_buffer == NULL */
    assert_int_equal(uds_client_request(&ctx, 0x10, payload, 1u, NULL), UDS_ERR_NOT_INIT);
}

static void test_client_request_invalid_arg(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* len > 0 with NULL data -> INVALID_ARG. */
    assert_int_equal(uds_client_request(&ctx, 0x10, NULL, 4u, NULL), UDS_ERR_INVALID_ARG);
}

static void test_client_request_buffer_too_small(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.tx_buffer_size = 4u; /* len+1 must fit; len=4 -> 5 > 4 */
    uint8_t payload[4] = {1, 2, 3, 4};
    assert_int_equal(uds_client_request(&ctx, 0x22, payload, 4u, NULL), UDS_ERR_BUFFER_TOO_SMALL);
}

static void test_client_request_sends_request(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    g_send_calls = 0;
    g_cb_calls = 0;
    uint8_t payload[] = {0xF1, 0x90};
    int rc = uds_client_request(&ctx, 0x22, payload, 2u, client_cb);
    assert_int_equal(rc, 0);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send_len, 3u); /* sid + 2 payload */
    assert_int_equal(g_last_send[0], 0x22);
    assert_int_equal(g_last_send[1], 0xF1);
    assert_int_equal(g_last_send[2], 0x90);
    assert_int_equal(ctx.client_pending_sid, 0x22);
}

/* data NULL with len 0 is allowed (skips the memcpy branch at :663). */
static void test_client_request_zero_len(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    g_send_calls = 0;
    int rc = uds_client_request(&ctx, 0x3E, NULL, 0u, NULL);
    assert_int_equal(rc, 0);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send_len, 1u);
    assert_int_equal(g_last_send[0], 0x3E);
}

/* Mutex hooks are invoked on the client_request path (:655, :669). */
static int g_lock_calls, g_unlock_calls;
static void mtx_lock(void *h)
{
    (void) h;
    g_lock_calls++;
}
static void mtx_unlock(void *h)
{
    (void) h;
    g_unlock_calls++;
}

static void test_client_request_takes_mutex(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    g_lock_calls = g_unlock_calls = g_send_calls = 0;
    uint8_t payload[] = {0x01};
    assert_int_equal(uds_client_request(&ctx, 0x10, payload, 1u, NULL), 0);
    assert_int_equal(g_lock_calls, 1);
    assert_int_equal(g_unlock_calls, 1);
}

/* ---- uds_init validation / strict / NVM ---- */

static void test_init_rejects_missing_callbacks(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    /* Missing get_time_ms / fn_tp_send / buffers -> INVALID_ARG (:510-511). */
    assert_int_equal(uds_init(&ctx, &cfg), UDS_ERR_INVALID_ARG);

    /* NULL ctx/config -> INVALID_ARG (:505-506). */
    assert_int_equal(uds_init(NULL, &cfg), UDS_ERR_INVALID_ARG);
    assert_int_equal(uds_init(&ctx, NULL), UDS_ERR_INVALID_ARG);
}

/* strict_compliance clamps too-small P2/P2* to the safe minimums (:527-532). */
static void test_init_strict_clamps_timing(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.strict_compliance = true;
    cfg.p2_ms = 1u;       /* below UDS_P2_MIN_SAFE_MS (20) */
    cfg.p2_star_ms = 10u; /* below UDS_P2_STAR_MIN_SAFE_MS (1000) */

    assert_int_equal(uds_init(&ctx, &cfg), UDS_OK);
    assert_int_equal(ctx.p2_ms, UDS_P2_MIN_SAFE_MS);
    assert_int_equal(ctx.p2_star_ms, UDS_P2_STAR_MIN_SAFE_MS);
}

static uint8_t g_init_nvm[2] = {UDS_SESSION_ID_EXTENDED, 0x02};
static int init_nvm_load(struct uds_ctx *ctx, uint8_t *state, uint16_t len)
{
    (void) ctx;
    if (len == 2u) {
        state[0] = g_init_nvm[0];
        state[1] = g_init_nvm[1];
        return 2;
    }
    return -1;
}

/* fn_nvm_load restores session/security at init (:537-543). */
static void test_init_loads_nvm_state(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_nvm_load = init_nvm_load;

    assert_int_equal(uds_init(&ctx, &cfg), UDS_OK);
    assert_int_equal(ctx.active_session, UDS_SESSION_ID_EXTENDED);
    assert_int_equal(ctx.security_level, 0x02);
}

/* ---- uds_process: mutex, S3 timeout, P2*, periodic ---- */

static void test_process_null_guard(void **state)
{
    (void) state;
    /* No crash on NULL ctx (:551-552). */
    uds_process(NULL);
    uds_ctx_t ctx;
    ctx.config = NULL;
    uds_process(&ctx); /* config NULL guard */
}

static void test_process_takes_mutex(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    g_lock_calls = g_unlock_calls = 0;
    g_time = 0;
    uds_process(&ctx);
    assert_int_equal(g_lock_calls, 1);
    assert_int_equal(g_unlock_calls, 1);
}

/* S3 timer reverts to default session after inactivity (:562-570). */
static void test_process_s3_timeout_reverts(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    ctx.active_session = UDS_SESSION_ID_EXTENDED;
    ctx.security_level = 1u;
    ctx.authenticated = true;
    ctx.last_msg_time = 0u;
    g_time = UDS_S3_TIMEOUT_MS + 1u;

    uds_process(&ctx);
    assert_int_equal(ctx.active_session, UDS_SESSION_ID_DEFAULT);
    assert_int_equal(ctx.security_level, 0u);
    assert_false(ctx.authenticated);
}

/* P2* expiry while pending emits a 0x78 ResponsePending (:574-594). */
static void test_process_p2_star_sends_pending(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    ctx.p2_msg_pending = true;
    ctx.p2_star_active = true;
    ctx.p2_timer_start = 0u;
    ctx.server_pending_sid = 0x31u;
    g_time = ctx.p2_star_ms + 1u;

    g_send_calls = 0;
    uds_process(&ctx);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send[0], 0x7F);
    assert_int_equal(g_last_send[1], 0x31);
    assert_int_equal(g_last_send[2], UDS_NRC_RESPONSE_PENDING);
    assert_int_equal(ctx.rcrrp_count, 1u);
}

/* RCRRP limit reached -> conditionsNotCorrect + mutex unlocked on early return
 * (:580-587). */
static void test_process_rcrrp_limit_aborts(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.rcrrp_limit = 2u;
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    ctx.p2_msg_pending = true;
    ctx.p2_star_active = true;
    ctx.p2_timer_start = 0u;
    ctx.server_pending_sid = 0x31u;
    ctx.rcrrp_count = 2u; /* already at limit */
    g_time = ctx.p2_star_ms + 1u;

    g_send_calls = g_lock_calls = g_unlock_calls = 0;
    uds_process(&ctx);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
    assert_int_equal(ctx.rcrrp_count, 0u);
    assert_int_equal(g_lock_calls, 1);
    assert_int_equal(g_unlock_calls, 1); /* unlocked on the early-return branch */
}

static int g_periodic_calls;
static int periodic_read(struct uds_ctx *ctx, uint8_t pid, uint8_t *out, uint16_t max)
{
    (void) ctx;
    (void) pid;
    (void) max;
    g_periodic_calls++;
    out[0] = 0xAB;
    out[1] = 0xCD;
    return 2;
}

/* Periodic scheduler emits when a slot's deadline has elapsed (:598-624). */
static void test_process_periodic_transmits(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_periodic_read = periodic_read;
    uds_init(&ctx, &cfg);

    ctx.periodic_count = 1u;
    ctx.periodic_ids[0] = 0x55u;
    ctx.periodic_rates[0] = UDS_PERIODIC_RATE_FAST;
    ctx.periodic_timers[0] = 0u; /* due immediately */
    g_time = 100u;

    g_send_calls = g_periodic_calls = 0;
    uds_process(&ctx);
    assert_int_equal(g_periodic_calls, 1);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send_len, 3u); /* id + 2 data */
    assert_int_equal(g_last_send[0], 0x55);
    /* Fast rate reschedules 100ms ahead. */
    assert_int_equal(ctx.periodic_timers[0], 100u + UDS_PERIODIC_FAST_INTERVAL_MS);
}

/* Medium/slow rate branches of the interval selector (:617-622). */
static void test_process_periodic_medium_and_slow_rate(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_periodic_read = periodic_read;
    uds_init(&ctx, &cfg);

    ctx.periodic_count = 2u;
    ctx.periodic_ids[0] = 0x60u;
    ctx.periodic_rates[0] = UDS_PERIODIC_RATE_MEDIUM;
    ctx.periodic_timers[0] = 0u;
    ctx.periodic_ids[1] = 0x61u;
    ctx.periodic_rates[1] = UDS_PERIODIC_RATE_SLOW;
    ctx.periodic_timers[1] = 0u;
    g_time = 50u;

    g_send_calls = g_periodic_calls = 0;
    uds_process(&ctx);
    assert_int_equal(g_periodic_calls, 2);
    assert_int_equal(ctx.periodic_timers[0], 50u + UDS_PERIODIC_MEDIUM_INTERVAL_MS);
    assert_int_equal(ctx.periodic_timers[1], 50u + UDS_PERIODIC_SLOW_INTERVAL_MS);
}

/* ---- Unknown session bit (uds_internal_session_bit default, :190-191) ---- */

/* With an invalid active_session, session_bit is 0, so a session-gated service
 * is rejected with serviceNotSupportedInActiveSession. */
static void test_unknown_session_bit_rejects(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.restrict_sessions = true; /* makes 0x27 require extended/programming */
    ctx.active_session = 0xFEu;   /* unknown -> session bit 0 */

    uint8_t req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x27);
    assert_int_equal(g_tx_buf[2], UDS_NRC_SERVICE_NOT_SUPP_IN_SESS);
}

/* ---- Secured data (0x84) error paths ---- */

/* fn_secure_decode reports an inner length of 0 -> incorrectLength (:421-423). */
static int decode_zero_len(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                           uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) in_len;
    (void) out;
    (void) out_max;
    return 0; /* decoded zero bytes */
}
static int encode_passthrough(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in,
                              uint16_t in_len, uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) out_max;
    memcpy(out, in, in_len);
    return (int) in_len;
}

static void test_secured_data_inner_len_zero(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = decode_zero_len;
    cfg.fn_secure_encode = encode_passthrough;

    uint8_t req[] = {0x84, 0x00, 0x00, 0xAA, 0xBB};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x84);
    assert_int_equal(g_tx_buf[2], UDS_NRC_INCORRECT_LENGTH);
}

/* fn_secure_encode returns negative -> NRC from the encode failure (:467-469). */
static int decode_to_tester_present(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in,
                                    uint16_t in_len, uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) in_len;
    (void) out_max;
    out[0] = 0x3E; /* TesterPresent */
    out[1] = 0x00;
    return 2;
}
static int encode_fail(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                       uint8_t *out, uint16_t out_max)
{
    (void) ctx;
    (void) apar;
    (void) in;
    (void) in_len;
    (void) out;
    (void) out_max;
    return -(int) UDS_NRC_CONDITIONS_NOT_CORRECT;
}

static void test_secured_data_encode_negative(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_secure_decode = decode_to_tester_present;
    cfg.fn_secure_encode = encode_fail;

    uint8_t req[] = {0x84, 0x00, 0x00, 0x11, 0x22};
    /* uds_input_sdu: 2; inner TesterPresent handler reads no extra time. */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x84);
    assert_int_equal(g_tx_buf[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
}

/* ---- uds_input_sdu_addr: mutex, busy-repeat, client response ---- */

/* Empty input takes the mutex and releases it on the early return (:687-693). */
static void test_input_empty_releases_mutex(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    g_lock_calls = g_unlock_calls = 0;
    uds_input_sdu(&ctx, NULL, 0u);
    assert_int_equal(g_lock_calls, 1);
    assert_int_equal(g_unlock_calls, 1);
}

/* A new request while a previous one is pending -> busyRepeatRequest (:703-715). */
static void test_input_busy_repeat(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    ctx.p2_msg_pending = true; /* a response is already pending */
    g_time = 100;

    g_send_calls = 0;
    uint8_t req[] = {0x22, 0xF1, 0x90};
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_last_send[0], 0x7F);
    assert_int_equal(g_last_send[2], UDS_NRC_BUSY_REPEAT_REQUEST);
}

/* A suppressed TesterPresent while pending just refreshes S3, no response,
 * mutex released (:704-709). */
static void test_input_busy_suppressed_tp(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    ctx.p2_msg_pending = true;
    g_time = 100;
    g_send_calls = g_lock_calls = g_unlock_calls = 0;
    uint8_t tp[] = {0x3E, 0x80}; /* suppress bit set */
    uds_input_sdu(&ctx, tp, sizeof(tp));
    assert_int_equal(g_send_calls, 0); /* no response */
    assert_int_equal(g_unlock_calls, 1);
}

/* A positive response to our outstanding client request invokes the callback
 * and clears the pending state (:719-733). */
static void test_input_client_positive_response(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    cfg.fn_mutex_lock = mtx_lock;
    cfg.fn_mutex_unlock = mtx_unlock;
    uds_init(&ctx, &cfg);

    /* Send a client request for 0x22 -> arms client_pending_sid. */
    g_send_calls = g_cb_calls = 0;
    uint8_t payload[] = {0xF1, 0x90};
    assert_int_equal(uds_client_request(&ctx, 0x22, payload, 2u, client_cb), 0);
    assert_int_equal(ctx.client_pending_sid, 0x22);

    /* Feed the positive response 0x62 ... -> callback fires, pending cleared. */
    g_lock_calls = g_unlock_calls = 0;
    uint8_t resp[] = {0x62, 0xF1, 0x90, 0xAB};
    uds_input_sdu(&ctx, resp, sizeof(resp));
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(ctx.client_pending_sid, 0u);
    assert_int_equal(g_unlock_calls, 1);
}

/* A negative response (0x7F <sid> ...) to our outstanding client request also
 * resolves the pending request (:721-733). */
static void test_input_client_negative_response(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = fixed_time;
    cfg.fn_tp_send = counting_send;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);
    uds_init(&ctx, &cfg);

    g_cb_calls = 0;
    uint8_t payload[] = {0x01};
    assert_int_equal(uds_client_request(&ctx, 0x10, payload, 1u, client_cb), 0);

    /* 0x7F 0x10 0x22: negative response carrying our pending SID. */
    uint8_t neg[] = {0x7F, 0x10, 0x22};
    uds_input_sdu(&ctx, neg, sizeof(neg));
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(ctx.client_pending_sid, 0u);
}

/* ---- uds_emit_response / uds_send_response / uds_send_nrc edge paths ---- */

static void test_emit_response_guards(void **state)
{
    (void) state;
    /* NULL ctx -> NOT_INIT (:752-753). */
    assert_int_equal(uds_emit_response(NULL, 4u), UDS_ERR_NOT_INIT);

    BEGIN_UDS_TEST(ctx, cfg);
    /* len > tx_buffer_size -> BUFFER_TOO_SMALL (:755-756). */
    assert_int_equal(uds_emit_response(&ctx, (uint16_t) (cfg.tx_buffer_size + 1u)),
                     UDS_ERR_BUFFER_TOO_SMALL);
}

/* uds_send_response with suppress flag set emits nothing and returns OK
 * (:774-782). */
static void test_send_response_suppressed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    ctx.suppress_pos_resp = true;
    /* No tp_send expected: suppressed. */
    assert_int_equal(uds_send_response(&ctx, 6u), UDS_OK);
    assert_false(ctx.suppress_pos_resp);
    assert_false(ctx.p2_msg_pending);
}

static void test_send_nrc_buffer_too_small(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.tx_buffer_size = 2u; /* < 3 -> BUFFER_TOO_SMALL (:793-794) */
    assert_int_equal(uds_send_nrc(&ctx, 0x22, UDS_NRC_REQUEST_OUT_OF_RANGE),
                     UDS_ERR_BUFFER_TOO_SMALL);
}

/* When capturing (inner 0x84/ROE dispatch), an NRC is captured into the capture
 * buffer rather than sent (:824-827). */
static void test_send_nrc_captured(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    uint8_t capture[8];
    ctx.secure_capturing = true;
    ctx.secure_capture_buf = capture;
    ctx.secure_capture_size = sizeof(capture);
    ctx.secure_capture_len = 0u;

    /* No tp_send expected: NRC is captured, not transmitted. */
    assert_int_equal(uds_send_nrc(&ctx, 0x22, UDS_NRC_REQUEST_OUT_OF_RANGE), UDS_OK);
    assert_int_equal(ctx.secure_capture_len, 3u);
    assert_int_equal(capture[0], 0x7F);
    assert_int_equal(capture[1], 0x22);
    assert_int_equal(capture[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_log_emitted_when_level_allowed),
        cmocka_unit_test(test_log_suppressed_when_level_too_high),
        cmocka_unit_test(test_client_request_not_init),
        cmocka_unit_test(test_client_request_invalid_arg),
        cmocka_unit_test(test_client_request_buffer_too_small),
        cmocka_unit_test(test_client_request_sends_request),
        cmocka_unit_test(test_client_request_zero_len),
        cmocka_unit_test(test_client_request_takes_mutex),
        cmocka_unit_test(test_init_rejects_missing_callbacks),
        cmocka_unit_test(test_init_strict_clamps_timing),
        cmocka_unit_test(test_init_loads_nvm_state),
        cmocka_unit_test(test_process_null_guard),
        cmocka_unit_test(test_process_takes_mutex),
        cmocka_unit_test(test_process_s3_timeout_reverts),
        cmocka_unit_test(test_process_p2_star_sends_pending),
        cmocka_unit_test(test_process_rcrrp_limit_aborts),
        cmocka_unit_test(test_process_periodic_transmits),
        cmocka_unit_test(test_process_periodic_medium_and_slow_rate),
        cmocka_unit_test(test_unknown_session_bit_rejects),
        cmocka_unit_test(test_input_empty_releases_mutex),
        cmocka_unit_test(test_input_busy_repeat),
        cmocka_unit_test(test_input_busy_suppressed_tp),
        cmocka_unit_test(test_input_client_positive_response),
        cmocka_unit_test(test_input_client_negative_response),
        cmocka_unit_test(test_emit_response_guards),
        cmocka_unit_test(test_send_response_suppressed),
        cmocka_unit_test(test_send_nrc_buffer_too_small),
        cmocka_unit_test(test_send_nrc_captured),
        cmocka_unit_test(test_secured_data_inner_len_zero),
        cmocka_unit_test(test_secured_data_encode_negative),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
