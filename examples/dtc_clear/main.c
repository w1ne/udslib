/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief ClearDiagnosticInformation (0x14) hook demo.
 *
 * Answers issue #77: a self-contained example of the
 *
 *     int (*fn_dtc_clear)(struct uds_ctx *ctx, uint32_t group);
 *
 * config hook. No reference store is used here — the ECU keeps its own tiny
 * DTC table and clears it by hand, so you can see exactly what the hook owns.
 *
 * The library handles the wire format: it validates the request length, parses
 * the 24-bit groupOfDTC argument, calls your hook, and turns the result into
 * either a positive 0x54 response or a 0x7F 14 <NRC> negative response. Your
 * hook only decides *which DTCs to drop* and *whether the conditions allow it*.
 *
 * The demo feeds four ClearDiagnosticInformation requests and prints the raw
 * response bytes for each:
 *
 *   1. Clear all groups (0xFFFFFF) while a precondition blocks it  -> 7F 14 22
 *   2. Clear all groups (0xFFFFFF) once conditions are correct      -> 54
 *   3. Clear one specific DTC by group                             -> 54
 *   4. Clear an unknown group                                      -> 7F 14 31
 *
 * Returns 0 if every step produced the expected response, non-zero otherwise.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_core.h"

/* Negative-response codes the hook may return (negated). See ISO 14229-1. */
#define NRC_CONDITIONS_NOT_CORRECT 0x22
#define NRC_REQUEST_OUT_OF_RANGE 0x31

/* The "clear everything" group argument defined by ISO 14229-1. */
#define DTC_GROUP_ALL 0xFFFFFFu

/* --- ECU state: a hand-rolled DTC table (no reference store) --- */
typedef struct
{
    uint32_t dtc; /* 24-bit DTC number */
    bool active;  /* set when a fault is stored, cleared by 0x14 */
} ecu_dtc_t;

typedef struct
{
    ecu_dtc_t dtcs[3];
    bool engine_running; /* a precondition the hook checks */
} ecu_state_t;

/*
 * The 0x14 hook. `group` is the 24-bit groupOfDTC from the request:
 *   - 0xFFFFFF clears all DTCs;
 *   - any other value clears only the matching DTC group/number.
 *
 * Return UDS_OK for a positive 0x54 response, or a negative NRC to make the
 * library emit 0x7F 14 <NRC>.
 */
static int ecu_clear_dtc(uds_ctx_t *ctx, uint32_t group)
{
    ecu_state_t *ecu = (ecu_state_t *) ctx->config->app_data;

    /* Refuse to clear fault memory while the engine is running: the diagnostic
     * preconditions are not met. */
    if (ecu->engine_running) {
        return -NRC_CONDITIONS_NOT_CORRECT;
    }

    if (group == DTC_GROUP_ALL) {
        for (size_t i = 0u; i < 3u; i++) {
            ecu->dtcs[i].active = false;
        }
        return UDS_OK;
    }

    /* Specific group: clear the matching DTC, reject if none matches. */
    for (size_t i = 0u; i < 3u; i++) {
        if (ecu->dtcs[i].dtc == group) {
            ecu->dtcs[i].active = false;
            return UDS_OK;
        }
    }
    return -NRC_REQUEST_OUT_OF_RANGE;
}

/* --- Transport glue: capture the response bytes --- */
static uint8_t g_resp[64];
static uint16_t g_resp_len;

static uint32_t ecu_time(void)
{
    return 0u;
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

/* Send one ClearDiagnosticInformation (0x14) request and print the response. */
static bool clear_dtc(uds_ctx_t *ctx, ecu_state_t *ecu, uint32_t group, const char *what)
{
    uint8_t req[4] = {0x14u, (uint8_t) (group >> 16), (uint8_t) (group >> 8), (uint8_t) group};
    g_resp_len = 0u;
    uds_input_sdu(ctx, req, sizeof(req));

    printf("  %-34s ->", what);
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }

    int active = 0;
    for (size_t i = 0u; i < 3u; i++) {
        active += ecu->dtcs[i].active ? 1 : 0;
    }
    if (g_resp_len >= 1u && g_resp[0] == 0x54u) {
        printf("   (positive, %d DTC(s) still active)\n", active);
    }
    else if (g_resp_len >= 3u && g_resp[0] == 0x7Fu) {
        printf("   (negative, NRC 0x%02X, %d DTC(s) untouched)\n", g_resp[2], active);
    }
    else {
        printf("   (unexpected)\n");
    }
    return g_resp_len >= 1u;
}

int main(void)
{
    /* Three stored, active DTCs. */
    ecu_state_t ecu = {
        .dtcs =
            {
                {0x012345u, true},
                {0xDCBA98u, true},
                {0xFFFF33u, true},
            },
        .engine_running = true,
    };

    static uint8_t rxb[64];
    static uint8_t txb[64];
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = ecu_time;
    cfg.fn_tp_send = ecu_send;
    cfg.rx_buffer = rxb;
    cfg.rx_buffer_size = sizeof(rxb);
    cfg.tx_buffer = txb;
    cfg.tx_buffer_size = sizeof(txb);
    cfg.app_data = &ecu;
    cfg.fn_dtc_clear = ecu_clear_dtc; /* <-- the hook from issue #77 */

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    printf("=== ClearDiagnosticInformation (0x14) ===\n");

    bool ok = true;

    /* 1. Engine running -> preconditions block the clear (ConditionsNotCorrect). */
    ok &= clear_dtc(&ctx, &ecu, DTC_GROUP_ALL, "clear all, engine running");
    ok &= (g_resp_len == 3u && g_resp[0] == 0x7Fu && g_resp[2] == NRC_CONDITIONS_NOT_CORRECT);

    /* 2. Engine off -> clear all groups succeeds. */
    ecu.engine_running = false;
    ok &= clear_dtc(&ctx, &ecu, DTC_GROUP_ALL, "clear all, engine off");
    ok &= (g_resp_len == 1u && g_resp[0] == 0x54u);

    /* Re-store a fault, then clear it by its specific group. */
    ecu.dtcs[1].active = true;
    ok &= clear_dtc(&ctx, &ecu, 0xDCBA98u, "clear DTC 0xDCBA98 (specific)");
    ok &= (g_resp_len == 1u && g_resp[0] == 0x54u);

    /* 4. Unknown group -> RequestOutOfRange. */
    ok &= clear_dtc(&ctx, &ecu, 0xAABBCCu, "clear DTC 0xAABBCC (unknown)");
    ok &= (g_resp_len == 3u && g_resp[0] == 0x7Fu && g_resp[2] == NRC_REQUEST_OUT_OF_RANGE);

    printf("\n%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
