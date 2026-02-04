/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* Port functions from uds_zephyr_port.c */
extern uint32_t uds_get_time_ms_zephyr(void);
extern void uds_log_zephyr(uint8_t level, const char* msg);

#if defined(CONFIG_UDSLIB_TRANSPORT_NATIVE)
extern int uds_zephyr_isotp_init(uint32_t rx_id, uint32_t tx_id);
extern int uds_zephyr_isotp_send(struct uds_ctx* ctx, const uint8_t* data, uint16_t len);
extern int uds_zephyr_isotp_recv(uint8_t* buf, uint16_t size);
#elif defined(CONFIG_UDSLIB_TRANSPORT_FALLBACK)
extern int uds_zephyr_tp_fallback_init(struct uds_ctx* uds_ctx, uint32_t rx_id, uint32_t tx_id);
#endif

/* Buffers */
static uint8_t uds_rx_buf[CONFIG_UDSLIB_RX_BUFFER_SIZE];
static uint8_t uds_tx_buf[CONFIG_UDSLIB_TX_BUFFER_SIZE];

static uds_ctx_t uds_ctx;

int main(void)
{
    printk("Starting LibUDS Zephyr Server Example (Fallback Mode)...\n");

    uds_tp_send_fn tp_send_func = NULL;

#if defined(CONFIG_UDSLIB_TRANSPORT_NATIVE)
    if (uds_zephyr_isotp_init(0x7E0, 0x7E8) < 0) {
        printk("Failed to init ISO-TP shim\n");
        return -1;
    }
    tp_send_func = uds_zephyr_isotp_send;
#elif defined(CONFIG_UDSLIB_TRANSPORT_FALLBACK)
    if (uds_zephyr_tp_fallback_init(&uds_ctx, 0x7E0, 0x7E8) < 0) {
        printk("Failed to init Fallback ISO-TP shim\n");
        return -1;
    }
    tp_send_func = uds_isotp_send;
#endif

    uds_config_t config = {.rx_buffer = uds_rx_buf,
                           .rx_buffer_size = sizeof(uds_rx_buf),
                           .tx_buffer = uds_tx_buf,
                           .tx_buffer_size = sizeof(uds_tx_buf),
                           .get_time_ms = uds_get_time_ms_zephyr,
                           .fn_tp_send = tp_send_func,
                           .fn_log = uds_log_zephyr,
                           .p2_ms = 50,
                           .p2_star_ms = 5000};

    if (uds_init(&uds_ctx, &config) != 0) {
        printk("Failed to init UDS stack\n");
        return -1;
    }

    printk("UDS Server ready. Waiting for requests (0x7E0 RX / 0x7E8 TX)...\n");

    while (1) {
#if defined(CONFIG_UDSLIB_TRANSPORT_NATIVE)
        uint8_t frame[CONFIG_UDSLIB_MAX_SDU_SIZE];
        int len = uds_zephyr_isotp_recv(frame, sizeof(frame));
        if (len > 0) {
            uds_input_sdu(&uds_ctx, frame, (uint16_t) len);
        }
#elif defined(CONFIG_UDSLIB_TRANSPORT_FALLBACK)
        /* ISO-TP is handled in interrupt callbacks, but we need to process timers/CFs */
        uds_tp_isotp_process(uds_get_time_ms_zephyr());
#endif
        uds_process(&uds_ctx);
        k_sleep(K_MSEC(1));
    }

    return 0;
}
