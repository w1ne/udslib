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
    uds_ctx_t ctx_ptr;                    \
    uds_config_t cfg_ptr;                 \
    setup_ctx(&ctx_ptr, &cfg_ptr)

void setup_ctx(uds_ctx_t *ctx, uds_config_t *cfg);

#endif /* TEST_HELPERS_H */
