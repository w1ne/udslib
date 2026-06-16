/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief End-to-end ECU reprogramming demo.
 *
 * A flash "tool" drives a LibUDS ECU server through the full ISO 14229
 * reprogramming sequence in a single process: enter programming session ->
 * security access (seed/key) -> link control -> access timing -> erase ->
 * request download -> transfer data -> transfer exit -> checksum routine.
 *
 * The ECU runs with config.restrict_sessions enabled, so the reprogramming
 * services are only reachable once in the programming session. Returns 0 when
 * the whole sequence succeeds, non-zero otherwise (used as a CI smoke test).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_core.h"

/* --- ECU state --- */
static uint8_t g_resp[512];
static uint16_t g_resp_len;
static uint32_t g_time;
static uint8_t g_flash[256];
static uint32_t g_flash_written;

static uint32_t ecu_time(void)
{
    return g_time;
}

static int ecu_send(uds_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    (void) ctx;
    memcpy(g_resp, data, len);
    g_resp_len = len;
    return 0;
}

static int ecu_seed(uds_ctx_t *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len)
{
    (void) ctx;
    (void) level;
    (void) max_len;
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

/* Trivial "key = seed XOR 0xFF" scheme for the demo. */
static int ecu_key(uds_ctx_t *ctx, uint8_t level, const uint8_t *seed, const uint8_t *key,
                   uint16_t key_len)
{
    (void) ctx;
    (void) level;
    if (key_len != 4u || seed == NULL) {
        return -0x35; /* invalidKey */
    }
    for (int i = 0; i < 4; i++) {
        if (key[i] != (uint8_t) (seed[i] ^ 0xFFu)) {
            return -0x35;
        }
    }
    return 0;
}

static int ecu_link(uds_ctx_t *ctx, uint8_t subfunction, uint32_t link_param)
{
    (void) ctx;
    printf("    [ECU] link control sub=0x%02X param=0x%X\n", subfunction, link_param);
    return 0;
}

static int ecu_request_download(uds_ctx_t *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    if (size > sizeof(g_flash)) {
        return -0x70; /* uploadDownloadNotAccepted */
    }
    g_flash_written = 0;
    printf("    [ECU] download accepted: addr=0x%08X size=%u\n", addr, size);
    return 0;
}

static int ecu_transfer(uds_ctx_t *ctx, uint8_t sequence, const uint8_t *data, uint32_t len)
{
    (void) ctx;
    (void) sequence;
    if ((g_flash_written + len) > sizeof(g_flash)) {
        return -0x71; /* transferDataSuspended */
    }
    memcpy(&g_flash[g_flash_written], data, len);
    g_flash_written += len;
    return 0;
}

static int ecu_transfer_exit(uds_ctx_t *ctx)
{
    (void) ctx;
    printf("    [ECU] transfer complete: %u bytes\n", g_flash_written);
    return 0;
}

static int ecu_routine(uds_ctx_t *ctx, uint8_t type, uint16_t id, const uint8_t *data, uint32_t len,
                       uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) type;
    (void) data;
    (void) len;
    (void) max_len;
    if (id == 0xFF00u) { /* erase memory */
        memset(g_flash, 0xFF, sizeof(g_flash));
        return 0;
    }
    if (id == 0x0202u) { /* compute checksum */
        uint8_t cs = 0u;
        for (uint32_t i = 0u; i < g_flash_written; i++) {
            cs ^= g_flash[i];
        }
        out_buf[0] = cs;
        return 1;
    }
    return -0x31; /* requestOutOfRange */
}

/* --- Flash tool driver --- */
static uds_ctx_t g_ecu;

/* Send one request to the ECU and return its captured response. */
static const uint8_t *tool_send(const uint8_t *req, uint32_t len, uint16_t *resp_len)
{
    g_resp_len = 0u;
    uds_input_sdu(&g_ecu, req, len);
    *resp_len = g_resp_len;
    return g_resp;
}

static int step(const char *name, const uint8_t *req, uint32_t len)
{
    uint16_t rl;
    const uint8_t *r = tool_send(req, len, &rl);
    if (rl >= 1u && r[0] == (uint8_t) (req[0] + 0x40u)) {
        printf("[TOOL] %-26s OK\n", name);
        return 0;
    }
    printf("[TOOL] %-26s FAILED ->", name);
    for (uint16_t i = 0u; i < rl; i++) {
        printf(" %02X", r[i]);
    }
    printf("\n");
    return 1;
}

int main(void)
{
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
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.restrict_sessions = true; /* reprogramming services gated to programming session */
    cfg.fn_security_seed = ecu_seed;
    cfg.fn_security_key = ecu_key;
    cfg.fn_link_control = ecu_link;
    cfg.fn_request_download = ecu_request_download;
    cfg.fn_transfer_data = ecu_transfer;
    cfg.fn_transfer_exit = ecu_transfer_exit;
    cfg.fn_routine_control = ecu_routine;
    uds_init(&g_ecu, &cfg);

    printf("=== LibUDS ECU reprogramming demo ===\n");
    int fail = 0;
    uint16_t rl;
    const uint8_t *r;

    /* 1. Programming session. */
    uint8_t sess[] = {0x10, 0x02};
    fail |= step("DiagnosticSession(prog)", sess, sizeof(sess));

    /* 2. Security access: request seed, answer with key = seed ^ 0xFF. */
    uint8_t req_seed[] = {0x27, 0x01};
    r = tool_send(req_seed, sizeof(req_seed), &rl);
    if (rl >= 6u && r[0] == 0x67u) {
        printf("[TOOL] %-26s OK seed=%02X%02X%02X%02X\n", "SecurityAccess(seed)", r[2], r[3], r[4],
               r[5]);
        uint8_t key[] = {0x27,
                         0x02,
                         (uint8_t) (r[2] ^ 0xFFu),
                         (uint8_t) (r[3] ^ 0xFFu),
                         (uint8_t) (r[4] ^ 0xFFu),
                         (uint8_t) (r[5] ^ 0xFFu)};
        fail |= step("SecurityAccess(key)", key, sizeof(key));
    }
    else {
        printf("[TOOL] SecurityAccess(seed)     FAILED\n");
        fail = 1;
    }

    /* 3. Link control: verify a baud transition, then apply it. */
    uint8_t lc_verify[] = {0x87, 0x01, 0x03};
    uint8_t lc_trans[] = {0x87, 0x03};
    fail |= step("LinkControl(verify)", lc_verify, sizeof(lc_verify));
    fail |= step("LinkControl(transition)", lc_trans, sizeof(lc_trans));

    /* 4. Access timing: P2 = 100ms, P2* = 2000ms. */
    uint8_t timing[] = {0x83, 0x04, 0x00, 0x64, 0x00, 0xC8};
    fail |= step("AccessTiming(set)", timing, sizeof(timing));

    /* 5. Erase routine. */
    uint8_t erase[] = {0x31, 0x01, 0xFF, 0x00};
    fail |= step("RoutineControl(erase)", erase, sizeof(erase));

    /* 6. Request download: addr 0x08000000, size 16, ALFID 0x44. */
    uint8_t dl[] = {0x34, 0x00, 0x44, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
    fail |= step("RequestDownload", dl, sizeof(dl));

    /* 7. Transfer two 8-byte blocks. */
    uint8_t blk1[] = {0x36, 0x01, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t blk2[] = {0x36, 0x02, 9, 10, 11, 12, 13, 14, 15, 16};
    fail |= step("TransferData #1", blk1, sizeof(blk1));
    fail |= step("TransferData #2", blk2, sizeof(blk2));

    /* 8. Transfer exit. */
    uint8_t exit_req[] = {0x37};
    fail |= step("RequestTransferExit", exit_req, sizeof(exit_req));

    /* 9. Checksum routine: read the result back. */
    uint8_t checksum[] = {0x31, 0x01, 0x02, 0x02};
    r = tool_send(checksum, sizeof(checksum), &rl);
    if (rl >= 5u && r[0] == 0x71u) {
        printf("[TOOL] %-26s OK checksum=0x%02X\n", "RoutineControl(checksum)", r[4]);
    }
    else {
        printf("[TOOL] RoutineControl(checksum) FAILED\n");
        fail = 1;
    }

    printf("=== %s ===\n", fail ? "REPROGRAMMING FAILED" : "Reprogramming sequence complete");
    return fail;
}
