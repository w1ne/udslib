/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief DTC store end-to-end demo.
 *
 * Registers the three DTCs from issue #39 into the reference store, wires
 * the store callbacks to a UDS stack, prints each DTC's category
 * (Powertrain / Network / Network), then feeds ReadDTCInformation:
 * 0x02 (DTC list), 0x04 (snapshot), and 0x06 (extended data).
 *
 * Returns 0 on success (positive response 0x59 received), non-zero otherwise.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

/* --- ECU glue --- */
static uint8_t g_resp[512];
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

static int serve(uds_ctx_t *ctx, const uint8_t *req, uint8_t req_len, const char *title)
{
    printf("\n=== %s ===\n", title);
    g_resp_len = 0u;
    uds_input_sdu(ctx, req, req_len);
    if ((g_resp_len < 1u) || (g_resp[0] != 0x59u)) {
        printf("  ERROR: no positive response (got %u bytes)\n", g_resp_len);
        return 1;
    }
    printf("  Response (%u bytes):", g_resp_len);
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }
    printf("\n");
    return 0;
}

/* Drive the fault-detection counter to +127. The store confirms the DTC on
 * that sample and, if an environment was published, freezes it. */
static void confirm_dtc(uds_dtc_store_t *store, uint32_t dtc)
{
    for (int i = 0; i < 200; i++) {
        uds_dtc_record_t *r = uds_dtc_store_get(store, dtc);
        if ((r == NULL) || ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u)) {
            return;
        }
        uds_dtc_store_report_test(store, dtc, true);
    }
}

/* --- Category label helper --- */
static const char *category_label(uds_dtc_category_t cat)
{
    switch (cat) {
        case UDS_DTC_POWERTRAIN:
            return "P (Powertrain)";
        case UDS_DTC_CHASSIS:
            return "C (Chassis)";
        case UDS_DTC_BODY:
            return "B (Body)";
        case UDS_DTC_NETWORK:
            return "U (Network)";
        default:
            return "?";
    }
}

int main(void)
{
    /* --- Initialise store --- */
    static uds_dtc_record_t backing[8];
    static uds_dtc_store_t store;
    uds_dtc_store_init(&store, backing, 8u, 40u);

    /* Register the three issue #39 DTCs. */
    uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                           UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_register(&store, 0xDCBA98u, UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT, 0x20u,
                           UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_register(&store, 0xFFFFFFu, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0x30u,
                           UDS_DTC_FGID_EMISSIONS);

    /* Mark all three as failed so they appear in a status-mask query. */
    uds_dtc_store_report_test(&store, 0x012345u, true);
    uds_dtc_store_report_test(&store, 0xDCBA98u, true);
    uds_dtc_store_report_test(&store, 0xFFFFFFu, true);

    /* Print each DTC with its category. */
    printf("=== DTC category classification ===\n");
    const uint32_t dtcs[3] = {0x012345u, 0xDCBA98u, 0xFFFFFFu};
    for (int i = 0; i < 3; i++) {
        uds_dtc_category_t cat = uds_dtc_category(dtcs[i]);
        printf("  DTC 0x%06X -> %s\n", dtcs[i], category_label(cat));
    }

    /* --- Wire store to UDS stack --- */
    static uint8_t rxb[512];
    static uint8_t txb[512];
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
    cfg.fn_dtc_clear = uds_dtc_store_clear_cb;

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    /* --- Feed ReadDTCInformation 0x02 0xFF (all failing DTCs) --- */
    uint8_t req_list[] = {0x19, 0x02, 0xFF};
    if (serve(&ctx, req_list, (uint8_t) sizeof(req_list),
              "ReadDTCInformation (0x19 0x02 0xFF) response") != 0) {
        return 1;
    }

    /* Decode and print each DTC record from the response.
     * Layout: 59 02 <statusAvail> [DTC_HI DTC_MID DTC_LO STATUS]* */
    uint16_t offset = 3u; /* skip 59 02 <avail> */
    int dtc_idx = 0;
    while ((offset + 4u) <= g_resp_len) {
        uint32_t dtc = ((uint32_t) g_resp[offset] << 16) | ((uint32_t) g_resp[offset + 1u] << 8) |
                       g_resp[offset + 2u];
        uint8_t status = g_resp[offset + 3u];
        printf("  DTC[%d] = 0x%06X  status=0x%02X  category=%s\n", dtc_idx, dtc, status,
               category_label(uds_dtc_category(dtc)));
        offset += 4u;
        dtc_idx++;
    }

    /* The stack does not call report_test or operation_cycle. The monitor
     * below confirms the DTC. Call operation_cycle when the ignition cycle ends.
     * Publish the environment first: confirmation copies it into the snapshot. */
    printf("\nThe stack does not call report_test. The monitor below confirms DTC 012345.\n");
    printf("Call uds_dtc_store_operation_cycle() when the ignition cycle ends.\n");
    uds_dtc_snapshot_t env;
    env.voltage = 0x8Cu; /* 14.0 V at 0.1 V per count */
    env.power_mode = 0x02u;
    env.time.year = 0x19u; /* 2025-06-19 13:05:42, years since 2000 */
    env.time.month = 0x06u;
    env.time.day = 0x13u;
    env.time.hour = 0x0Du;
    env.time.minute = 0x05u;
    env.time.second = 0x2Au;
    uds_dtc_store_set_environment(&store, &env);
    confirm_dtc(&store, 0x012345u);

    uint8_t req_snap[] = {0x19, 0x04, 0x01, 0x23, 0x45, 0x01};
    if (serve(&ctx, req_snap, (uint8_t) sizeof(req_snap),
              "Snapshot (0x19 0x04) for DTC 012345") != 0) {
        return 1;
    }
    /* 59 04 DTC(3) status rec n DID 1001 time(6) DID 1002 voltage power */
    if ((g_resp_len != 20u) || (g_resp[6] != 0x01u) || (g_resp[10] != 0x19u) ||
        (g_resp[18] != 0x8Cu) || (g_resp[19] != 0x02u)) {
        printf("  ERROR: snapshot bytes do not match the published environment.\n");
        return 1;
    }
    printf("  DID 0x1001 time 2025-06-19 13:05:42, DID 0x1002 voltage 14.0 V, power mode 2.\n");

    uint8_t req_ext[] = {0x19, 0x06, 0x01, 0x23, 0x45, 0x01};
    if (serve(&ctx, req_ext, (uint8_t) sizeof(req_ext),
              "Extended data (0x19 0x06) for DTC 012345") != 0) {
        return 1;
    }
    /* status, record 0x01, occurrence, pending, aged, ageing */
    if ((g_resp_len != 11u) || (g_resp[6] != 0x01u) || (g_resp[7] != 0x01u) ||
        (g_resp[8] != 0x01u) || (g_resp[9] != 0x00u) || (g_resp[10] != 0x00u)) {
        printf("  ERROR: extended-data counters are not occurrence=1 pending=1.\n");
        return 1;
    }
    printf("  Counters: occurrence 1, pending 1, aged 0, ageing 0.\n");

    return 0;
}
