/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_isotp_coverage.c
 * @brief Regression coverage for uds_tp_isotp.c edge paths: NULL guards on
 *        the public API, send-frame failure with no can_send, the N_Bs arm +
 *        expire transition, oversized-SDU send, invalid escape First Frames,
 *        and a consecutive frame with the wrong sequence number.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

static uint32_t g_time;
static uint32_t time_src(void)
{
    return g_time;
}

static int g_can_calls;
static int ok_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    (void) data;
    (void) len;
    g_can_calls++;
    return 0;
}

static void make_uds(uds_ctx_t *uds, uds_config_t *cfg, uint8_t *rxb, uint16_t rxn)
{
    memset(uds, 0, sizeof(*uds));
    memset(cfg, 0, sizeof(*cfg));
    cfg->get_time_ms = time_src;
    cfg->rx_buffer = rxb;
    cfg->rx_buffer_size = rxn;
    uds->config = cfg;
}

/* Every public ISO-TP entry point must tolerate a NULL context. */
static void test_null_guards(void **state)
{
    (void) state;
    uint8_t buf[8] = {0};

    /* init NULL: no crash. */
    uds_tp_isotp_init(NULL, ok_can_send, 0x1, 0x2, buf, sizeof(buf));
    /* setters NULL. */
    uds_tp_isotp_set_fd(NULL, true);
    uds_tp_isotp_set_pad_byte(NULL, 0xAA);
    uds_tp_isotp_set_mode(NULL, ISOTP_FULL_DUPLEX);
    uds_tp_isotp_set_functional_id(NULL, 0x123);
    /* send / process NULL -> defined returns. */
    assert_int_equal(uds_isotp_send(NULL, buf, sizeof(buf)), -1);
    uds_tp_isotp_process(NULL, 0);

    /* rx_callback NULL iso / NULL data / zero len -> guarded (:488-489). */
    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[16];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));
    uds_isotp_rx_callback(NULL, &uds, 0x2, buf, sizeof(buf));

    uds_isotp_ctx_t iso;
    uds_tp_isotp_init(&iso, ok_can_send, 0x1, 0x2, buf, sizeof(buf));
    uds_isotp_rx_callback(&iso, &uds, 0x2, NULL, 4);
    uds_isotp_rx_callback(&iso, &uds, 0x2, buf, 0);
    /* No assertion target beyond "did not crash"; assert the setters' state is
     * untouched on the live instance to make the test concrete. */
    assert_int_equal(iso.tx_id, 0x1);
    assert_int_equal(iso.rx_id, 0x2);
}

/* uds_internal_tp_send_frame returns -1 when can_send is NULL; a Single Frame
 * send therefore fails (:32-35 false branch). */
static void test_send_frame_without_can_send(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[16];
    uds_tp_isotp_init(&iso, NULL, 0x1, 0x2, sdu, sizeof(sdu)); /* can_send NULL */

    uint8_t payload[3] = {0x22, 0xF1, 0x90};
    assert_int_equal(uds_isotp_send(&iso, payload, sizeof(payload)), -1);
}

/* uds_send_mf rejects an SDU larger than the TX cache (:149-150). */
static void test_send_mf_oversize(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[8]; /* tiny TX cache */
    uds_tp_isotp_init(&iso, ok_can_send, 0x1, 0x2, sdu, sizeof(sdu));

    uint8_t big[20];
    memset(big, 0xAA, sizeof(big));
    assert_int_equal(uds_isotp_send(&iso, big, sizeof(big)), -2);
}

/* uds_send_mf returns -1 when the FF frame send fails (:203-204). */
static int failing_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    (void) data;
    (void) len;
    return -7;
}
static void test_send_mf_ff_failure(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, failing_can_send, 0x1, 0x2, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), -1);
}

/* N_Bs timer: armed at time 0 on first WAIT_FC tick, then expires (:246-253). */
static void test_n_bs_arm_at_zero_then_expire(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x1, 0x2, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* First tick at time 0: timer arms to 1 (not 0, which means "unarmed"). */
    uds_tp_isotp_process(&iso, 0);
    assert_int_equal(iso.timer_n_bs, 1u);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Past N_Bs from the armed base of 1: abort to IDLE. */
    uds_tp_isotp_process(&iso, 1u + ISOTP_N_BS_DEFAULT_MS + 1u);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    assert_int_equal(iso.timer_n_bs, 0u);
}

/* Escape FF with too few bytes for the 32-bit length is ignored (:374-375). */
static void test_rx_escape_ff_too_short(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[64];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    /* Escape FF marker (0x10 0x00) but only 4 bytes total (< 6 needed). */
    uint8_t ff[4] = {0x10, 0x00, 0x00, 0x00};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, sizeof(ff));
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE); /* ignored */
}

/* Escape FF whose FF_DL <= 4095 is invalid and ignored (:379-380). */
static void test_rx_escape_ff_invalid_length(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[64];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    /* Escape marker but FF_DL = 0x00000010 (16) <= 4095 -> invalid. */
    uint8_t ff[8] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x10, 0xAA, 0xBB};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, sizeof(ff));
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE);
}

/* A consecutive frame with the wrong sequence number aborts to IDLE (:429-431). */
static void test_rx_cf_wrong_sn(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uds_ctx_t uds;
    uds_config_t cfg;
    uint8_t rxb[64];
    make_uds(&uds, &cfg, rxb, sizeof(rxb));

    g_time = 100;
    /* FF for a 20-byte SDU; engine expects CF SN=1 next. */
    uint8_t ff[8] = {0x10, 0x14, 1, 2, 3, 4, 5, 6};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, ff, sizeof(ff));
    assert_int_equal(iso.rx_state, ISOTP_RX_WAIT_CF);

    /* CF with SN=5 (expected 1) -> reset to IDLE. */
    uint8_t cf[8] = {0x25, 7, 8, 9, 10, 11, 12, 13};
    uds_isotp_rx_callback(&iso, &uds, 0x7E8, cf, sizeof(cf));
    assert_int_equal(iso.rx_state, ISOTP_RX_IDLE);
}

/* A Flow Control frame shorter than 3 bytes is ignored (:455-456). */
static void test_rx_fc_too_short(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);

    /* FC frame of only 2 bytes -> ignored, still waiting. */
    uint8_t fc[2] = {0x30, 0x00};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, sizeof(fc));
    assert_int_equal(iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* Full TX flow: FF -> FC(CTS, STmin in the 0xF1-0xF9 microsecond band) -> CFs
 * until the message completes, exercising the STmin decode and the
 * SENDING_CF "remaining == 0" exit (:261-262, :272-273). */
static void test_tx_full_flow_stmin_microsecond(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xAA, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);

    /* FC CTS with BS=0 (unlimited) and STmin=0xF5 (microsecond band -> 1ms). */
    uint8_t fc[3] = {0x30, 0x00, 0xF5};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, sizeof(fc));
    assert_int_equal(iso.tx_state, ISOTP_TX_SENDING_CF);

    /* Drive process() across enough time to flush both CFs (FF carried 6 bytes,
     * 14 remain -> two CFs of 7). STmin decodes to 1ms. */
    g_can_calls = 0;
    uds_tp_isotp_process(&iso, 100);
    uds_tp_isotp_process(&iso, 200);
    uds_tp_isotp_process(&iso, 300);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    assert_true(g_can_calls >= 2);

    /* A further tick on the now-idle channel exercises the remaining==0 exit. */
    uds_tp_isotp_process(&iso, 400);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
}

/* STmin > 0x7F (reserved) decodes to 0 (no separation) (:275-276). */
static void test_tx_stmin_reserved(void **state)
{
    (void) state;
    uds_isotp_ctx_t iso;
    uint8_t sdu[64];
    uds_tp_isotp_init(&iso, ok_can_send, 0x7E0, 0x7E8, sdu, sizeof(sdu));

    uint8_t data[20];
    memset(data, 0xBB, sizeof(data));
    assert_int_equal(uds_isotp_send(&iso, data, sizeof(data)), 0);

    /* FC CTS, STmin=0xAB (reserved/invalid -> 0). */
    uint8_t fc[3] = {0x30, 0x00, 0xAB};
    uds_isotp_rx_callback(&iso, NULL, 0x7E8, fc, sizeof(fc));

    g_can_calls = 0;
    uds_tp_isotp_process(&iso, 10);
    uds_tp_isotp_process(&iso, 11);
    uds_tp_isotp_process(&iso, 12);
    assert_int_equal(iso.tx_state, ISOTP_TX_IDLE);
    assert_true(g_can_calls >= 2);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_null_guards),
        cmocka_unit_test(test_send_frame_without_can_send),
        cmocka_unit_test(test_send_mf_oversize),
        cmocka_unit_test(test_send_mf_ff_failure),
        cmocka_unit_test(test_n_bs_arm_at_zero_then_expire),
        cmocka_unit_test(test_rx_escape_ff_too_short),
        cmocka_unit_test(test_rx_escape_ff_invalid_length),
        cmocka_unit_test(test_rx_cf_wrong_sn),
        cmocka_unit_test(test_rx_fc_too_short),
        cmocka_unit_test(test_tx_full_flow_stmin_microsecond),
        cmocka_unit_test(test_tx_stmin_reserved),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
