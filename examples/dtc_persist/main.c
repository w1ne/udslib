/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief DTC status kept across a reset, without the library touching flash.
 *
 * The "NVM" is a plain byte array. A real ECU replaces nvm_save() / nvm_load()
 * with its own flash or EEPROM driver. The UDS stack only sees the RAM store.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

static uint8_t g_nvm[64];
static uint16_t g_nvm_len;

static uint8_t g_resp[64];
static uint16_t g_resp_len;
static uint32_t g_time;

static uint32_t ecu_time(void)
{
    return g_time;
}

static int ecu_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    if (len > sizeof(g_resp)) {
        len = (uint16_t) sizeof(g_resp);
    }
    memcpy(g_resp, data, len);
    g_resp_len = len;
    return 0;
}

/* Application NVM. Not part of udslib. */
static int nvm_save(const uds_dtc_store_t *store)
{
    int n = uds_dtc_store_serialize(store, g_nvm, (uint16_t) sizeof(g_nvm));
    if (n < 0) {
        return n;
    }
    g_nvm_len = (uint16_t) n;
    return 0;
}

static int nvm_load(uds_dtc_store_t *store)
{
    return uds_dtc_store_deserialize(store, g_nvm, g_nvm_len);
}

static void register_catalog(uds_dtc_store_t *store)
{
    uds_dtc_store_register(store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                           UDS_DTC_FGID_EMISSIONS);
}

static int serve_19(uds_ctx_t *ctx)
{
    uint8_t req[] = {0x19, 0x02, 0xFF};
    g_resp_len = 0u;
    uds_input_sdu(ctx, req, sizeof(req));
    printf("  19 02 FF ->");
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }
    printf("\n");
    return (g_resp_len > 0u && g_resp[0] == 0x59u) ? 0 : -1;
}

/* Response contains DTC 012345 with status 0x23 (failed this cycle). */
static int response_has_stored_dtc(void)
{
    for (uint16_t i = 3u; (i + 4u) <= g_resp_len; i = (uint16_t) (i + 4u)) {
        uint32_t dtc = ((uint32_t) g_resp[i] << 16) | ((uint32_t) g_resp[i + 1u] << 8) |
                       (uint32_t) g_resp[i + 2u];
        if ((dtc == 0x012345u) && (g_resp[i + 3u] == 0x23u)) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;
    static uint8_t rxb[128];
    static uint8_t txb[128];

    uds_dtc_store_init(&store, backing, 4u, 40u);
    register_catalog(&store);
    uds_dtc_store_report_test(&store, 0x012345u, true);
    if (nvm_save(&store) != 0) {
        printf("NVM save failed\n");
        return 1;
    }
    printf("Saved %u bytes of DTC state into the application NVM buffer.\n", g_nvm_len);

    /* Power cycle: RAM is gone. The catalog is registered again from the ECU
     * tables. Runtime status is still zero until the blob is loaded. */
    memset(backing, 0, sizeof(backing));
    uds_dtc_store_init(&store, backing, 4u, 40u);
    register_catalog(&store);

    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = ecu_time;
    cfg.fn_tp_send = ecu_send;
    cfg.rx_buffer = rxb;
    cfg.rx_buffer_size = sizeof(rxb);
    cfg.tx_buffer = txb;
    cfg.tx_buffer_size = sizeof(txb);
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;
    cfg.app_data = &store;
    cfg.fn_dtc_list = uds_dtc_store_list_cb;

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    printf("After reset, before NVM load:\n");
    if (serve_19(&ctx) != 0) {
        return 1;
    }
    if (response_has_stored_dtc()) {
        printf("  ERROR: DTC survived in RAM. The demo did not reset the store.\n");
        return 1;
    }
    printf("  DTC is gone. 0x19 reads RAM, and RAM was cleared.\n");

    if (nvm_load(&store) < 0) {
        printf("NVM load failed\n");
        return 1;
    }

    printf("After NVM load:\n");
    if (serve_19(&ctx) != 0) {
        return 1;
    }
    if (!response_has_stored_dtc()) {
        printf("  ERROR: stored DTC status was not restored.\n");
        return 1;
    }
    printf("  DTC 012345 status 0x23 is back. Flash stayed in the application.\n");
    return 0;
}
