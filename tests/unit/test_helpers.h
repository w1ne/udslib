/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_helpers.h
 * @brief Shared Test Helpers and Mocks for CMocka Unit Tests
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"

/* --- Global Mock Buffers --- */

/** Global TX buffer for mock transport */
extern uint8_t g_tx_buf[1024];

/** Global RX buffer for mock stack */
extern uint8_t g_rx_buf[1024];

/* --- Mock Functions --- */

/**
 * @brief Mock implementation of uds_get_time_fn.
 * @return Value provided by will_return().
 */
uint32_t mock_get_time(void);

/**
 * @brief Mock implementation of uds_tp_send_fn.
 * @return Value provided by will_return().
 */
int mock_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* --- Test Setup Helpers --- */

/**
 * @brief Initialize a UDS context and config with mock defaults.
 *
 * @param ctx Pointer to the context to initialize.
 * @param cfg Pointer to the configuration to initialize/attach.
 */
#define BEGIN_UDS_TEST(ctx_ptr, cfg_ptr) \
    uds_ctx_t ctx_ptr;                   \
    uds_config_t cfg_ptr;                \
    setup_ctx(&ctx_ptr, &cfg_ptr)

void setup_ctx(uds_ctx_t *ctx, uds_config_t *cfg);

/* --- Shared negative-response assertion helper ---
 *
 * Drive a single request through the stack and assert the 3-byte negative
 * response 7F <sid> <nrc>. Every uds_input_sdu() call consumes two
 * mock_get_time values (last_msg_time and p2_timer_start); services that read
 * the clock internally (e.g. 0x27) need a third, which `extra_time` provides.
 *
 * This was previously copy-pasted, in three slightly different forms, into
 * test_service_coverage{,2,3}.c. Keep the single definition here.
 */
static inline __attribute__((unused)) void expect_nrc(uds_ctx_t *ctx, uint8_t *req, uint16_t len,
                                                      uint8_t sid, uint8_t nrc, int extra_time)
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

#endif /* TEST_HELPERS_H */
