#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "uds/uds_core.h"
#include <string.h>

/* Global Mock Buffers */
extern uint8_t g_tx_buf[1024];
extern uint8_t g_rx_buf[1024];

/* Mocks */
uint32_t mock_get_time(void);
int mock_tp_send(uds_ctx_t* ctx, const uint8_t* data, uint16_t len);

/* Helpers */
void setup_ctx(uds_ctx_t* ctx, uds_config_t* cfg);

#endif
