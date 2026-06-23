/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_service_did.c
 * @brief Unit tests for Table-Driven DID Registry (0x22/0x2E)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static uint8_t g_val_8 = 0x11;
static char g_str[10] = "OLD";

static int mock_did_read_fn(uds_ctx_t *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    if (did == 0x0100) {
        buf[0] = 0xAA;
        return 0;
    }
    return -1;
}

static int mock_did_write_fn(uds_ctx_t *ctx, uint16_t did, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    if (did == 0x0200 && len == 3) {
        memcpy(g_str, data, 3);
        return 0;
    }
    return -1;
}

static const uds_did_entry_t g_test_dids[] = {
    {0xF190, 8, UDS_SESSION_ALL, 0, NULL, NULL, &g_val_8},          /* Direct Storage */
    {0x0100, 1, UDS_SESSION_ALL, 0, mock_did_read_fn, NULL, NULL},  /* Read Callback */
    {0x0200, 3, UDS_SESSION_ALL, 0, NULL, mock_did_write_fn, NULL}, /* Write Callback */
    {0x5EC1, 4, UDS_SESSION_ALL, 0x04, NULL, NULL, &g_val_8},       /* Security Level 2 Required */
    {0x7701, 1, UDS_SESSION_PROGRAMMING, 0, NULL, NULL, &g_val_8},  /* Programming session only */
};

static const uds_did_table_t g_test_table = {.entries = g_test_dids, .count = 5};

static void test_rdbi_single_did_success(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0xF1, 0x90};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 11); /* 0x62 + F1 90 + 8 bytes */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[1], 0xF1);
    assert_int_equal(g_tx_buf[2], 0x90);
    assert_int_equal(g_tx_buf[3], 0x11);
}

static void test_rdbi_callback_success(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0x01, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x62);
    assert_int_equal(g_tx_buf[3], 0xAA);
}

static void test_wdbi_callback_success(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;
    strcpy(g_str, "OLD");

    uint8_t request[] = {0x2E, 0x02, 0x00, 'N', 'E', 'W'};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x6E);
    assert_string_equal(g_str, "NEW");
}

static void test_rdbi_invalid_did_nrc(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t request[] = {0x22, 0x99, 0x99};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x31);
}

static void test_rdbi_security_denied(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;
    ctx.security.level = 1; /* Locked (assuming Level 2 required for 0xSEC1) */

    /* 0xSEC1 = 0x5E 0xC1 */
    uint8_t request[] = {0x22, 0x5E, 0xC1};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x22);
    assert_int_equal(g_tx_buf[2], 0x33); /* Security Access Denied */
}

static void test_wdbi_security_denied(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;
    ctx.security.level = 1;

    /* 0x5EC1 requires Level 2 (mask 0x04) */
    uint8_t request[] = {0x2E, 0x5E, 0xC1, 0x01, 0x02, 0x03, 0x04};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x2E);
    assert_int_equal(g_tx_buf[2], 0x33);
}

static void test_wdbi_length_fail_nrc13(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    /* 0xF190 expects 8 bytes. Send 2. */
    uint8_t request[] = {0x2E, 0xF1, 0x90, 0x11, 0x22};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x2E);
    assert_int_equal(g_tx_buf[2], 0x13); /* NRC 0x13: Incorrect Length */
}

/* A DID restricted to the programming session must be readable there and
   rejected (0x31) in the extended session -- the two sessions must not be
   confused with each other. */
static void test_rdbi_session_gating(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_test_table;

    uint8_t req[] = {0x22, 0x77, 0x01};

    /* Programming session: allowed. */
    ctx.session.active = 0x02; /* programming */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 62 77 01 + 1 data byte */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[0], 0x62);

    /* Extended session: must be rejected, not silently allowed. */
    ctx.session.active = 0x03; /* extended */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 22 31 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[2], 0x31);
}

/* --- Finding 1 regression: a response larger than UDS_TX_FLUSH_SNAPSHOT_MAX
 * (default 512) must be delivered intact AND transmitted while the state lock is
 * still held. ---
 *
 * The fast path snapshots a frame onto the stack and sends it with the lock
 * released; an oversized frame cannot be snapshotted, so it is sent under the
 * lock (baseline send-under-lock behaviour) to keep a concurrent context from
 * tearing the shared tx_buffer mid-send. This test reads a 600-byte DID so the
 * 0x62 response is 603 bytes (> 512), verifies the whole frame reaches the
 * transport unmodified, and — via lock-tracking mutex hooks — asserts the lock
 * was HELD during fn_tp_send.
 */
#define BIG_DID_SIZE 600u

static uint8_t g_big_storage[BIG_DID_SIZE];

static int big_did_read_fn(uds_ctx_t *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    if (did == 0x9000) {
        for (uint16_t i = 0u; i < BIG_DID_SIZE; i++) {
            buf[i] = (uint8_t) ((i * 7u + 3u) & 0xFFu); /* deterministic pattern */
        }
        return 0;
    }
    return -1;
}

static const uds_did_entry_t g_big_dids[] = {
    {0x9000, BIG_DID_SIZE, UDS_SESSION_ALL, 0, big_did_read_fn, NULL, NULL},
};
static const uds_did_table_t g_big_table = {.entries = g_big_dids, .count = 1};

/* Lock-tracking OSAL hooks: record whether the lock was held when fn_tp_send ran. */
static int g_lock_depth;
static int g_lock_held_during_send;

static void track_lock(void *h)
{
    (void) h;
    g_lock_depth++;
}
static void track_unlock(void *h)
{
    (void) h;
    g_lock_depth--;
}

static uint8_t g_big_tx[1024];
static uint16_t g_big_sent_len;

static int big_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_lock_held_during_send = (g_lock_depth > 0) ? 1 : 0;
    g_big_sent_len = len;
    if (len > sizeof(g_big_tx)) {
        len = (uint16_t) sizeof(g_big_tx);
    }
    memcpy(g_big_tx, data, len);
    return 0;
}

static void test_rdbi_oversize_response_sent_under_lock(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.did_table = g_big_table;
    cfg.fn_tp_send = big_tp_send;
    cfg.fn_mutex_lock = track_lock;
    cfg.fn_mutex_unlock = track_unlock;
    g_lock_depth = 0;
    g_lock_held_during_send = -1;
    g_big_sent_len = 0;
    memset(g_big_tx, 0, sizeof(g_big_tx));
    memset(g_big_storage, 0, sizeof(g_big_storage));

    uint8_t request[] = {0x22, 0x90, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    uds_input_sdu(&ctx, request, sizeof(request));

    /* Full 603-byte frame delivered intact: header + the deterministic payload. */
    assert_int_equal(g_big_sent_len, (uint16_t) (3u + BIG_DID_SIZE));
    assert_int_equal(g_big_tx[0], 0x62);
    assert_int_equal(g_big_tx[1], 0x90);
    assert_int_equal(g_big_tx[2], 0x00);
    for (uint16_t i = 0u; i < BIG_DID_SIZE; i++) {
        assert_int_equal(g_big_tx[3u + i], (uint8_t) ((i * 7u + 3u) & 0xFFu));
    }

    /* The oversized frame was transmitted with the lock STILL HELD (it is too big
     * to snapshot), and the lock balanced back to zero afterwards. */
    assert_int_equal(g_lock_held_during_send, 1);
    assert_int_equal(g_lock_depth, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rdbi_single_did_success),
        cmocka_unit_test(test_rdbi_session_gating),
        cmocka_unit_test(test_rdbi_callback_success),
        cmocka_unit_test(test_wdbi_callback_success),
        cmocka_unit_test(test_rdbi_invalid_did_nrc),
        cmocka_unit_test(test_rdbi_security_denied),
        cmocka_unit_test(test_wdbi_security_denied),
        cmocka_unit_test(test_wdbi_length_fail_nrc13),
        cmocka_unit_test(test_rdbi_oversize_response_sent_under_lock),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
