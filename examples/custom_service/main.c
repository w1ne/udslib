/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Worked example: add a manufacturer-specific service WITHOUT editing
 *        the library, by registering it through config.user_services.
 *
 * The dispatcher checks user services before core services (see find_service()
 * in src/core/uds_core.c), so the same mechanism both *adds* new SIDs and
 * *overrides* a built-in one. This example adds a vendor SID 0xBA and shows a
 * core service (0x3E TesterPresent) still working alongside it.
 *
 * Self-contained and deterministic: the transport just prints the response,
 * so the program's stdout is stable enough to assert in CI.
 */

#include "uds/uds_core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint8_t g_tx[256];
static uint8_t g_rx[256];

static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

/* Transport sink: print the SDU the stack wants to send. */
static int on_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    printf("  <- response:");
    for (uint16_t i = 0u; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");
    return 0;
}

/*
 * Custom manufacturer-specific service, SID 0xBA (ISO reserves 0xBA-0xBE for
 * vendor use). This handler is the ENTIRE integration — no library file is
 * touched. It echoes the sub-function and returns a small vendor payload.
 */
static int handle_vendor_diag(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, 0xBAu, 0x13u); /* incorrectMessageLength */
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (0xBAu + 0x40u); /* positive response SID = 0xFA */
    tx[1] = data[1];                   /* echo sub-function */
    tx[2] = 'V';
    tx[3] = 'N';
    tx[4] = '1'; /* vendor payload */
    return uds_send_response(ctx, 5u);
}

/*
 * The registration table. Columns mirror uds_service_entry_t:
 *   { SID, min_len, session_mask, security_mask, handler, sub_mask }
 * Add a row here to add a service; use an existing SID to override a core one.
 */
static const uds_service_entry_t user_services[] = {
    {0xBAu, 2u, UDS_SESSION_ALL, 0u, handle_vendor_diag, NULL},
};

static void send_request(uds_ctx_t *ctx, const uint8_t *req, uint16_t len)
{
    printf("-> request:");
    for (uint16_t i = 0u; i < len; i++) {
        printf(" %02X", req[i]);
    }
    printf("\n");
    uds_input_sdu(ctx, req, len);
}

int main(void)
{
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = get_time_ms;
    cfg.fn_tp_send = on_tp_send;
    cfg.rx_buffer = g_rx;
    cfg.rx_buffer_size = sizeof(g_rx);
    cfg.tx_buffer = g_tx;
    cfg.tx_buffer_size = sizeof(g_tx);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;

    /* This is the whole extensibility wiring: */
    cfg.user_services = user_services;
    cfg.user_service_count = (uint16_t) (sizeof(user_services) / sizeof(user_services[0]));

    uds_ctx_t ctx;
    if (uds_init(&ctx, &cfg) != UDS_OK) {
        printf("uds_init failed\n");
        return 1;
    }

    printf("=== Custom vendor service 0xBA (registered via cfg.user_services) ===\n");
    const uint8_t vendor_req[] = {0xBA, 0x01};
    send_request(&ctx, vendor_req, sizeof(vendor_req)); /* expect: FA 01 56 4E 31 */

    printf("\n=== Core service 0x3E TesterPresent still dispatched normally ===\n");
    const uint8_t tp_req[] = {0x3E, 0x00};
    send_request(&ctx, tp_req, sizeof(tp_req)); /* expect: 7E 00 */

    printf("\n=== Unknown SID 0xC0 rejected with NRC 0x11 ===\n");
    const uint8_t bad_req[] = {0xC0, 0x00};
    send_request(&ctx, bad_req, sizeof(bad_req)); /* expect: 7F C0 11 */

    return 0;
}
