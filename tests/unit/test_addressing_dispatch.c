/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Captured server output (the response frame, if any). */
static uint8_t g_tx[64];
static uint16_t g_tx_len;
static int g_tx_calls;

static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_tx_calls++;
    if (len <= sizeof(g_tx)) {
        memcpy(g_tx, data, len);
        g_tx_len = len;
    }
    return 0;
}

static uint32_t g_time;
static uint32_t time_ms(void)
{
    return g_time;
}

/* A service that echoes a fixed positive response (0x40|SID). */
static void svc_ok(struct uds_ctx *ctx, const uint8_t *data, uint16_t len, uds_result_t *out);

/* A service that always returns NRC 0x22 (conditionsNotCorrect). */
static void svc_nrc22(struct uds_ctx *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) ctx;
    (void) data;
    (void) len;
    uds_nrc(out, 0x22u); /* conditionsNotCorrect */
}

/*
 * sub_mask for 0xA3: bit 1 set => only subfunction 0x01 is allowed.
 * A request with subfunction 0x02 will trigger NRC 0x12.
 */
static const uint8_t k_sub_only01[16] = {0x02, 0};

/* User services: 0xA0 = both (unset), 0xA1 = physical-only, 0xA2 = functional-only,
 * 0xA3 = sub_mask (0x12 path), 0xA4 = always NRC 0x22. */
static const uds_service_entry_t k_services[] = {
    {0xA0u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, 0u},                  /* both */
    {0xA1u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, UDS_ADDR_PHYSICAL},   /* physical only */
    {0xA2u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, UDS_ADDR_FUNCTIONAL}, /* functional only */
    {0xA3u, 2u, UDS_SESSION_ALL, 0u, svc_ok, k_sub_only01, 0u}, /* sub_mask triggers 0x12 */
    {0xA4u, 1u, UDS_SESSION_ALL, 0u, svc_nrc22, NULL, 0u}       /* returns NRC 0x22 */
};

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx[64];
static uint8_t g_txbuf[64];

static void svc_ok(struct uds_ctx *ctx, const uint8_t *data, uint16_t len, uds_result_t *out)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    uds_ok(out, 1u);
}

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.rx_buffer = g_rx;
    g_cfg.rx_buffer_size = sizeof(g_rx);
    g_cfg.tx_buffer = g_txbuf;
    g_cfg.tx_buffer_size = sizeof(g_txbuf);
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    g_cfg.user_services = k_services;
    g_cfg.user_service_count = 5u;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);
    g_time = 0;
    g_tx_len = 0;
    g_tx_calls = 0;
    return 0;
}

/* 6: address_mode==0 answers BOTH physical and functional. */
static void test_both_default(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA0u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE0u); /* 0xA0|0x40 */

    g_tx_calls = 0;
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE0u);
}

/* 7: physical-only service — functional request is silent, physical answers. */
static void test_physical_only(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA1u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 0); /* silent */

    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE1u);
}

/* 8: functional-only service — physical request -> NRC 0x11; functional answers. */
static void test_functional_only(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA2u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0x7Fu); /* negative response */
    assert_int_equal(g_tx[1], 0xA2u);
    assert_int_equal(g_tx[2], 0x11u); /* serviceNotSupported */

    g_tx_calls = 0;
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE2u);
}

/* 9: functional request for an UNKNOWN SID -> 0x11 suppressed (silent);
      same request physical -> 0x11 emitted. */
static void test_suppress_unknown_sid(void **state)
{
    (void) state;
    uint8_t req[1] = {0xBFu}; /* not in any table */
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 0); /* 0x11 suppressed */

    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[2], 0x11u);
}

/* 12: legacy uds_input_sdu still dispatches as physical. */
static void test_legacy_entry_physical(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA1u}; /* physical-only */
    uds_input_sdu(&g_ctx, req, 1u);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE1u); /* answered => treated as physical */
}

/* 10: functional request for sub-function 0x02 when only 0x01 is allowed -> NRC 0x12
 *     suppressed (silent); same request physical -> 0x12 emitted. */
static void test_suppress_0x12_functional(void **state)
{
    (void) state;
    uint8_t req[2] = {0xA3u, 0x02u}; /* subfunction 0x02 not in sub_mask */
    uds_input_sdu_addr(&g_ctx, req, 2u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 0); /* 0x12 suppressed on functional */

    uds_input_sdu_addr(&g_ctx, req, 2u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0x7Fu);
    assert_int_equal(g_tx[1], 0xA3u);
    assert_int_equal(g_tx[2], 0x12u); /* subFunctionNotSupported */
}

/* 11: non-suppressable NRC 0x22 (conditionsNotCorrect) must still be emitted on
 *     a functional request — guards against over-suppression. */
static void test_non_suppressable_nrc_functional(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA4u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 1); /* NOT suppressed */
    assert_int_equal(g_tx[0], 0x7Fu);
    assert_int_equal(g_tx[1], 0xA4u);
    assert_int_equal(g_tx[2], 0x22u); /* conditionsNotCorrect */
}

/* 13: ROE inner dispatch must not be silenced by a stale functional req_addr_mode.
 *
 * Sequence:
 *   (a) Setup ROE onChangeOfDataIdentifier(DID=0) with serviceToRespondTo = {0xA1}
 *       (physical-only service) and start it — both via physical addressing.
 *   (b) Feed a functional request for 0xA0 (both-mode service) to SET
 *       ctx->req_addr_mode = UDS_ADDR_FUNCTIONAL, simulating a prior functional broadcast.
 *   (c) Call uds_roe_trigger() which internally calls uds_internal_dispatch_captured()
 *       -> handle_request().  Without Fix A the addressing gate sees req_addr_mode ==
 *       UDS_ADDR_FUNCTIONAL, 0xA1 is physical-only, and silently returns — cap == 0,
 *       no 0xC6 frame is emitted.  With Fix A, req_addr_mode is forced to PHYSICAL
 *       for the duration of the inner dispatch, 0xA1 succeeds, and 0xC6 is sent.
 */
static void test_roe_inner_dispatch_not_silenced_by_stale_functional(void **state)
{
    (void) state;

    /* (a) Setup ROE: onChangeOfDataIdentifier(0x0000) -> 0xA1 (physical-only).
     *     86 03 <window=0x02 infinite> <DID hi=0x00> <DID lo=0x00> <STR=0xA1>
     *     Use physical addressing so the setup ACK is not suppressed. */
    uint8_t roe_setup[] = {0x86u, 0x03u, 0x02u, 0x00u, 0x00u, 0xA1u};
    uds_input_sdu_addr(&g_ctx, roe_setup, sizeof(roe_setup), UDS_ADDR_PHYSICAL);
    /* C6 03 <count=1> + echo of request body (data[2..5], 4 bytes) = 3 + 4 = 7 */
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xC6u);
    g_tx_calls = 0;

    /* Start ROE (86 05) via physical. */
    uint8_t roe_start[] = {0x86u, 0x05u};
    uds_input_sdu_addr(&g_ctx, roe_start, sizeof(roe_start), UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1); /* ACK */
    g_tx_calls = 0;

    /* (b) Feed a functional request for 0xA0 (both-mode service) to pollute
     *     req_addr_mode with UDS_ADDR_FUNCTIONAL. */
    uint8_t func_req[] = {0xA0u};
    uds_input_sdu_addr(&g_ctx, func_req, sizeof(func_req), UDS_ADDR_FUNCTIONAL);
    g_tx_calls = 0; /* discard the 0xE0 positive response */
    g_tx_len = 0;

    /* (c) Trigger the ROE event. Without Fix A the inner 0xA1 dispatch is silently
     *     dropped (stale functional mode hits the physical-only gate) and no 0xC6
     *     frame appears. With Fix A it must appear. */
    int emitted = uds_roe_trigger(&g_ctx, 0x03u, 0x0000u);
    assert_int_equal(emitted, 1);
    assert_int_equal(g_tx_calls, 1);  /* exactly one 0xC6 frame */
    assert_int_equal(g_tx[0], 0xC6u); /* ResponseOnEvent positive response */
    assert_int_equal(g_tx[1], 0x03u); /* event_type = onChangeOfDataIdentifier */
    assert_int_equal(g_tx[2], 0x01u); /* numberOfIdentifiedEvents */
    assert_int_equal(g_tx[3], (uint8_t) (0xA1u + 0x40u)); /* inner positive response SID */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_both_default, setup, NULL),
        cmocka_unit_test_setup_teardown(test_physical_only, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_only, setup, NULL),
        cmocka_unit_test_setup_teardown(test_suppress_unknown_sid, setup, NULL),
        cmocka_unit_test_setup_teardown(test_legacy_entry_physical, setup, NULL),
        cmocka_unit_test_setup_teardown(test_suppress_0x12_functional, setup, NULL),
        cmocka_unit_test_setup_teardown(test_non_suppressable_nrc_functional, setup, NULL),
        cmocka_unit_test_setup_teardown(test_roe_inner_dispatch_not_silenced_by_stale_functional,
                                        setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
