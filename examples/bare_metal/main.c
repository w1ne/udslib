/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Bare Metal Example (Super Loop)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* --- Hardware Abstraction (Stubbed) --- */
static uint32_t get_system_time_ms(void)
{
    /* TODO: Return ms from systick or timer */
    return 0;
}

static int can_send_frame(const uint8_t *data, uint16_t len)
{
    /* TODO: Write to CAN controller registers */
    return 0;
}

/* --- UDS Glue --- */
static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buffer[4096];
static uint8_t tx_buffer[4096];

static int tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    /* For Bare Metal without ISO-TP stack, use valid TP implementation */
    /* If using raw CAN, we need uds_tp_isotp.c (not shown here to keep simple) */
    /* Or assume 'data' is a CAN frame if len <= 8? No, UDSLib sends SDUs. */
    /* Real implementation needs an ISO-TP layer */
    return can_send_frame(data, len);
}

int main(void)
{
    /* 1. Setup Config */
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = get_system_time_ms;
    cfg.fn_tp_send = tp_send;
    cfg.rx_buffer = rx_buffer;
    cfg.rx_buffer_size = sizeof(rx_buffer);
    cfg.tx_buffer = tx_buffer;
    cfg.tx_buffer_size = sizeof(tx_buffer);

    /* 2. Init UDS */
    uds_init(&ctx, &cfg);

    /* 3. Super Loop */
    while (1) {
        /* A. Poll Hardware */
        /* if (CAN_Rx_Available()) {
             uds_input_sdu(uds_input_sdu(&ctx, data, len)ctx, data, len, 0);
           } */

        /* B. Run Stack */
        uds_process(&ctx);

        /* C. Delay/Sleep */
        /* wait_for_interrupt(); */
    }
    return 0;
}
