/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"
#include "uds_internal.h" /* UDS_NRC_INVALID_KEY, UDS_NRC_EXCEEDED_ATTEMPTS */

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
    (void) key;
    (void) key_len;
    if (key[0] == 0xDF && key[1] == 0xAE && key[2] == 0xBF && key[3] == 0xF0) {
        return 0;
    }
    return -0x35; /* Invalid Key */
}

static void test_security_access_seed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;

    uint8_t request[] = {0x27, 0x01};

    /* 3 calls to get_time_ms: 2 in uds_input_sdu, 1 in handler */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x67 01 + Seed(4) */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x01);
}

static void test_security_access_key_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;

    /* 1. Request a seed first to arm the requestSeed -> sendKey sequence. */
    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 0x67 01 + Seed(4) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));

    /* 2. Send the matching key. (Key for seed DE AD BE EF is DF AE BF F0.) */
    uint8_t request[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x67 02 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(ctx.security.level, 1);
    assert_int_equal(g_tx_buf[0], 0x67);
    assert_int_equal(g_tx_buf[1], 0x02);
}

/* The key verifier must receive the exact seed the server issued. */
static int g_seed_seen_by_key = 0;
static int mock_key_inspects_seed(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                                  const uint8_t *key, uint16_t key_len)
{
    (void) ctx;
    (void) level;
    (void) key;
    (void) key_len;
    if (seed && seed[0] == 0xDE && seed[1] == 0xAD && seed[2] == 0xBE && seed[3] == 0xEF) {
        g_seed_seen_by_key = 1;
    }
    return 0;
}

static void test_security_key_receives_issued_seed(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_key_inspects_seed;
    g_seed_seen_by_key = 0;

    uint8_t seed_req[] = {0x27, 0x01};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));

    uint8_t key_req[] = {0x27, 0x02, 0x11, 0x22, 0x33, 0x44};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, key_req, sizeof(key_req));

    assert_int_equal(g_seed_seen_by_key, 1);
}

static void test_security_access_delay_timer(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;
    cfg.security_max_attempts = 1;
    cfg.security_delay_ms = 1000;

    uint8_t seed_req[] = {0x27, 0x01};
    uint8_t request_fail[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
    uint8_t request_ok[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};

    /* 0. Request a seed to arm the sequence. */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, seed_req, sizeof(seed_req));

    /* 1. Fail first attempt */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 36 (Exceeded attempts) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_fail, sizeof(request_fail));
    assert_int_equal(g_tx_buf[2], 0x36);
    assert_int_equal(ctx.security.delay_end, 2000);

    /* 2. Try again before delay expires */
    will_return(mock_get_time, 1500);
    will_return(mock_get_time, 1500);
    will_return(mock_get_time, 1500);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 37 (Required delay) */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_ok, sizeof(request_ok));
    assert_int_equal(g_tx_buf[2], 0x37);

    /* 3. Try again after delay expires */
    will_return(mock_get_time, 2500);
    will_return(mock_get_time, 2500);
    will_return(mock_get_time, 2500);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x67 02 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, request_ok, sizeof(request_ok));
    assert_int_equal(g_tx_buf[0], 0x67);
}

/* ISO 14229-1: sendKey without a preceding requestSeed must be rejected with
   NRC 0x24 (requestSequenceError), and must not unlock anything. */
static void test_security_access_key_without_seed_rejected(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;

    uint8_t key_req[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 27 24 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, key_req, sizeof(key_req));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x27);
    assert_int_equal(g_tx_buf[2], 0x24);
    assert_int_equal(ctx.security.level, 0);
}

/* §5d regression: when security_max_attempts and security_delay_ms are both
 * zero (unconfigured), the implementation must fall back to the named defaults
 * UDS_DEFAULT_SECURITY_MAX_ATTEMPTS (3) and UDS_DEFAULT_SECURITY_DELAY_MS
 * (10000).  Exercise the full 3-strike path and verify NRC 0x36 is issued and
 * the lockout delay is armed on the third failure. */
static void test_security_access_default_lockout(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed;
    cfg.fn_security_key = mock_security_key;
    /* Intentionally leave security_max_attempts and security_delay_ms at zero
     * so the defaults (3 / 10000 ms) are exercised. */

    uint8_t seed_req[] = {0x27, 0x01};
    uint8_t bad_key[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};

#define SEND_SEED()                                      \
    do {                                                 \
        will_return(mock_get_time, 1000);                \
        will_return(mock_get_time, 1000);                \
        will_return(mock_get_time, 1000);                \
        expect_any(mock_tp_send, data);                  \
        expect_value(mock_tp_send, len, 6);              \
        will_return(mock_tp_send, 0);                    \
        uds_input_sdu(&ctx, seed_req, sizeof(seed_req)); \
    } while (0)

#define SEND_BAD_KEY(expect_len)                       \
    do {                                               \
        will_return(mock_get_time, 1000);              \
        will_return(mock_get_time, 1000);              \
        will_return(mock_get_time, 1000);              \
        expect_any(mock_tp_send, data);                \
        expect_value(mock_tp_send, len, (expect_len)); \
        will_return(mock_tp_send, 0);                  \
        uds_input_sdu(&ctx, bad_key, sizeof(bad_key)); \
    } while (0)

    /* Attempt 1: NRC 0x35 (InvalidKey), no lockout yet */
    SEND_SEED();
    SEND_BAD_KEY(3); /* 7F 27 35 */
    assert_int_equal(g_tx_buf[2], UDS_NRC_INVALID_KEY);
    assert_int_equal(ctx.security.delay_end, 0u);

    /* Attempt 2: NRC 0x35 again */
    SEND_SEED();
    SEND_BAD_KEY(3);
    assert_int_equal(g_tx_buf[2], UDS_NRC_INVALID_KEY);
    assert_int_equal(ctx.security.delay_end, 0u);

    /* Attempt 3: hits the default max (3) -> NRC 0x36, delay armed */
    SEND_SEED();
    SEND_BAD_KEY(3); /* 7F 27 36 */
    assert_int_equal(g_tx_buf[2], UDS_NRC_EXCEEDED_ATTEMPTS);
    /* delay_end = now(1000) + UDS_DEFAULT_SECURITY_DELAY_MS(10000) = 11000 */
    assert_int_equal(ctx.security.delay_end, 11000u);

#undef SEND_SEED
#undef SEND_BAD_KEY
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_security_access_seed),
        cmocka_unit_test(test_security_access_key_success),
        cmocka_unit_test(test_security_key_receives_issued_seed),
        cmocka_unit_test(test_security_access_delay_timer),
        cmocka_unit_test(test_security_access_key_without_seed_rejected),
        cmocka_unit_test(test_security_access_default_lockout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
