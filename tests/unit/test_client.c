/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "test_helpers.h"
#include "uds/uds_client.h"

static uint8_t g_sent[64];
static uint16_t g_sent_len;
static int client_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx; /* client transports get ctx == NULL */
    for (uint16_t i = 0u; (i < len) && (i < sizeof(g_sent)); i++) {
        g_sent[i] = data[i];
    }
    g_sent_len = len;
    return 0;
}

static int g_cb_calls;
static uint8_t g_cb_sid;
static uint8_t g_cb_payload0;
static void client_cb(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) c;
    g_cb_calls++;
    g_cb_sid = sid;
    g_cb_payload0 = (len > 0u) ? data[0] : 0xFFu;
}

static void make_client(uds_client_ctx_t *c, uds_config_t *cfg, uint8_t *txbuf, uint16_t txsz)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->tx_buffer = txbuf;
    cfg->tx_buffer_size = txsz;
    cfg->fn_tp_send = client_send;
    memset(c, 0, sizeof(*c));
    c->config = cfg;
    g_sent_len = 0u;
    g_cb_calls = 0;
}

static void test_client_request_frames_and_sends(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));

    uint8_t payload[] = {0xF1, 0x90};
    int rc = uds_client_request(&c, 0x22, payload, sizeof(payload), client_cb);
    assert_int_equal(rc, 0);
    assert_int_equal(g_sent_len, 3u); /* SID + 2 */
    assert_int_equal(g_sent[0], 0x22u);
    assert_int_equal(g_sent[1], 0xF1u);
    assert_int_equal(g_sent[2], 0x90u);
    assert_int_equal(c.pending_sid, 0x22u);
}

static void test_client_positive_response_fires_cb(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* positive response 0x62 <payload> */
    uint8_t resp[] = {0x62, 0xAB};
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_true(consumed);
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(g_cb_sid, 0x62u);
    assert_int_equal(g_cb_payload0, 0xABu); /* payload after SID */
    assert_int_equal(c.pending_sid, 0u);    /* cleared */
}

static void test_client_nrc_response_fires_cb(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* negative response 7F 22 31 (requestOutOfRange) */
    uint8_t resp[] = {0x7F, 0x22, 0x31};
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_true(consumed);
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(c.pending_sid, 0u);
}

static void test_client_non_matching_frame_not_consumed(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* an unrelated positive (0x50, a 0x10 response) must NOT be consumed */
    uint8_t other[] = {0x50, 0x01};
    bool consumed = uds_client_handle_response(&c, other[0], other, sizeof(other));
    assert_false(consumed);
    assert_int_equal(g_cb_calls, 0);
    assert_int_equal(c.pending_sid, 0x22u); /* still pending */
}

static void test_client_second_response_after_done_not_consumed(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);
    uint8_t resp[] = {0x62, 0xAB};
    (void) uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    /* a duplicate response after completion is ignored */
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_false(consumed);
    assert_int_equal(g_cb_calls, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_client_request_frames_and_sends),
        cmocka_unit_test(test_client_positive_response_fires_cb),
        cmocka_unit_test(test_client_nrc_response_fires_cb),
        cmocka_unit_test(test_client_non_matching_frame_not_consumed),
        cmocka_unit_test(test_client_second_response_after_done_not_consumed),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
