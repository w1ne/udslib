/**
 * @file test_helpers.c
 * @brief Implementation of Shared Test Helpers
 */

#include "test_helpers.h"

uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

uint32_t mock_get_time(void)
{
    return (uint32_t)mock();
}

int mock_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    check_expected_ptr(data);
    check_expected(len);
    memcpy(g_tx_buf, data, len);
    return (int)mock();
}

void setup_ctx(uds_ctx_t *ctx, uds_config_t *cfg)
{
    memset(cfg, 0, sizeof(uds_config_t));
    cfg->get_time_ms = mock_get_time;
    cfg->fn_tp_send = mock_tp_send;
    cfg->rx_buffer = g_rx_buf;
    cfg->rx_buffer_size = sizeof(g_rx_buf);
    cfg->tx_buffer = g_tx_buf;
    cfg->tx_buffer_size = sizeof(g_tx_buf);
    cfg->p2_ms = 50;
    cfg->p2_star_ms = 5000;

    uds_init(ctx, cfg);
}
