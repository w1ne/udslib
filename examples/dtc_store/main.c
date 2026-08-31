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
 * (Powertrain / Network / Network), then feeds a ReadDTCInformation 0x19 0x02
 * 0xFF request and prints the response bytes.
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
    cfg.fn_dtc_clear = uds_dtc_store_clear_cb;

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    /* --- Feed ReadDTCInformation 0x02 0xFF (all failing DTCs) --- */
    printf("\n=== ReadDTCInformation (0x19 0x02 0xFF) response ===\n");
    uint8_t req[] = {0x19, 0x02, 0xFF};
    g_resp_len = 0u;
    uds_input_sdu(&ctx, req, sizeof(req));

    if (g_resp_len < 1u || g_resp[0] != 0x59u) {
        printf("  ERROR: no positive response (got %u bytes)\n", g_resp_len);
        return 1;
    }

    printf("  Response (%u bytes):", g_resp_len);
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }
    printf("\n");

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

    return 0;
}
