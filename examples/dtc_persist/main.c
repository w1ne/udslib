/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief ECU-side DTC monitor, operation cycle, and NVM, without flash in udslib.
 *
 * The "NVM" is a byte array. A real ECU replaces nvm_save() / nvm_load() with
 * its own flash or EEPROM driver. The UDS stack only reads the RAM store.
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

static uds_dtc_snapshot_t sample_environment(void)
{
    uds_dtc_snapshot_t env;
    env.voltage = 0x8Cu; /* application scale, e.g. 14.0 V */
    env.power_mode = 0x02u;
    env.time.second = 0x2Au;
    env.time.minute = 0x05u;
    env.time.hour = 0x0Du;
    env.time.day = 0x13u;
    env.time.month = 0x06u;
    env.time.year = 0x19u; /* years since 2000 */
    return env;
}

/* The ECU monitor. The UDS stack never calls this. */
static void monitor_until_confirmed(uds_dtc_store_t *store, uint32_t dtc)
{
    uds_dtc_snapshot_t env = sample_environment();
    uds_dtc_store_set_environment(store, &env);
    for (int i = 0; i < 127; i++) {
        uds_dtc_store_report_test(store, dtc, true);
    }
}

static int serve(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, const char *label)
{
    g_resp_len = 0u;
    uds_input_sdu(ctx, req, len);
    printf("  %s ->", label);
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }
    printf("\n");
    return (g_resp_len > 0u && g_resp[0] == 0x59u) ? 0 : -1;
}

static int expect_bytes(const uint8_t *want, uint16_t want_len)
{
    if ((g_resp_len != want_len) || (memcmp(g_resp, want, want_len) != 0)) {
        printf("  ERROR: unexpected response\n");
        return -1;
    }
    return 0;
}

int main(void)
{
    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;
    static uint8_t rxb[128];
    static uint8_t txb[128];

    printf("The UDS stack does not detect faults. The ECU monitor calls report_test.\n");
    uds_dtc_store_init(&store, backing, 4u, 40u);
    register_catalog(&store);
    monitor_until_confirmed(&store, 0x012345u);
    printf("127 failing reports confirmed DTC 012345 and stored the freeze frame.\n");

    /* End the failed ignition cycle, then one later cycle with no failure.
     * The stack does not call this either. Ageing advances only on the clean cycle. */
    uds_dtc_store_operation_cycle(&store);
    uds_dtc_store_operation_cycle(&store);
    uds_dtc_record_t *live = uds_dtc_store_get(&store, 0x012345u);
    printf("After two operation cycles, the second with no failure:\n");
    printf("  occurrence=%u  pending=%u  ageing=%u  snapshot voltage=0x%02X\n",
           live->extended.fault_occur_counter, live->extended.fault_pending_counter,
           live->extended.ageing_counter, live->snapshot.voltage);

    /* Save from the application task, not from inside the 0x19 handler.
     * Erasing flash there is slow enough that the tester times out. */
    if (nvm_save(&store) != 0) {
        printf("NVM save failed\n");
        return 1;
    }
    printf("Saved %u bytes into the application NVM buffer. The library did not write flash.\n",
           g_nvm_len);

    /* Power cycle. Register the catalog again. Runtime bytes stay zero until load. */
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
    cfg.fn_dtc_snapshot = uds_dtc_store_snapshot_cb;
    cfg.fn_dtc_extdata = uds_dtc_store_extdata_cb;

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    const uint8_t req_02[] = {0x19, 0x02, 0xFF};
    const uint8_t req_04[] = {0x19, 0x04, 0x01, 0x23, 0x45, 0x01};
    const uint8_t req_06[] = {0x19, 0x06, 0x01, 0x23, 0x45, 0x01};

    printf("\nAfter reset, before NVM load:\n");
    if (serve(&ctx, req_02, sizeof(req_02), "19 02 FF") != 0) {
        return 1;
    }
    if (serve(&ctx, req_04, sizeof(req_04), "19 04 01 23 45 01") != 0) {
        return 1;
    }
    if (serve(&ctx, req_06, sizeof(req_06), "19 06 01 23 45 01") != 0) {
        return 1;
    }
    printf("  RAM was cleared. 0x19 reads RAM.\n");

    if (nvm_load(&store) < 0) {
        printf("NVM load failed\n");
        return 1;
    }

    printf("\nAfter NVM load:\n");
    if (serve(&ctx, req_02, sizeof(req_02), "19 02 FF") != 0) {
        return 1;
    }
    const uint8_t want_02[] = {0x59, 0x02, 0x7F, 0x01, 0x23, 0x45, 0x2D};
    if (expect_bytes(want_02, sizeof(want_02)) != 0) {
        return 1;
    }
    if (serve(&ctx, req_04, sizeof(req_04), "19 04 01 23 45 01") != 0) {
        return 1;
    }
    const uint8_t want_04[] = {0x59, 0x04, 0x01, 0x23, 0x45, 0x2D, 0x01, 0x02, 0x10, 0x01,
                               0x19, 0x06, 0x13, 0x0D, 0x05, 0x2A, 0x10, 0x02, 0x8C, 0x02};
    if (expect_bytes(want_04, sizeof(want_04)) != 0) {
        return 1;
    }
    if (serve(&ctx, req_06, sizeof(req_06), "19 06 01 23 45 01") != 0) {
        return 1;
    }
    const uint8_t want_06[] = {0x59, 0x06, 0x01, 0x23, 0x45, 0x2D, 0x01, 0x01, 0x00, 0x00, 0x01};
    if (expect_bytes(want_06, sizeof(want_06)) != 0) {
        return 1;
    }
    printf("  Status, freeze frame, and counters are back.\n");
    return 0;
}
