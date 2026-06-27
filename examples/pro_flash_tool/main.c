/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief End-to-end ECU reprogramming demo - the canonical 17-step flash flow.
 *
 * A flash "tool" drives a LibUDS ECU server through the full OEM reprogramming
 * sequence that every automotive bootloader follows (ISO 14229-1 / ISO 14229-3).
 * The whole exchange runs in a single process: the tool builds each request,
 * hands it to the ECU's UDS stack, and inspects the response - exactly the bytes
 * a real diagnostic tester would put on the CAN bus.
 *
 * The 17 steps:
 *
 *    Pre-programming (default -> extended session)
 *     1. 0x10 0x03  DiagnosticSessionControl  -> extendedDiagnosticSession
 *     2. 0x85 0x02  ControlDTCSetting         -> OFF  (stop logging faults)
 *     3. 0x28 0x03  CommunicationControl      -> disableRxAndTx (quiet the bus)
 *
 *    Programming session + unlock
 *     4. 0x10 0x02  DiagnosticSessionControl  -> programmingSession
 *     5. 0x27 0x01  SecurityAccess            -> requestSeed
 *     6. 0x27 0x02  SecurityAccess            -> sendKey
 *     7. 0x2E F15A  WriteDataByIdentifier     -> programming fingerprint
 *
 *    Download
 *     8. 0x31 0x01 FF00  RoutineControl       -> eraseMemory
 *     9. 0x34            RequestDownload      -> addr + size
 *    10. 0x36 0x01       TransferData         -> block #1
 *    11. 0x36 0x02       TransferData         -> block #2
 *    12. 0x37            RequestTransferExit
 *    13. 0x31 0x01 0202  RoutineControl       -> checkMemory (CRC / dependencies)
 *
 *    Finalize (activate + restore)
 *    14. 0x11 0x01  ECUReset                  -> hardReset (boot the new app)
 *    15. 0x10 0x03  DiagnosticSessionControl  -> extendedDiagnosticSession
 *    16. 0x28 0x00  CommunicationControl      -> enableRxAndTx (restore the bus)
 *    17. 0x85 0x01  ControlDTCSetting         -> ON  (resume fault logging)
 *
 * The ECU runs with config.restrict_sessions enabled, so the reprogramming
 * services are only reachable once in the programming session. Returns 0 when
 * the whole sequence succeeds, non-zero otherwise (used as a CI smoke test).
 *
 * See README.md for how each step maps onto a real STM32F103 + bxCAN target.
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

/* Application-visible side effects of the maintenance services. A real ECU
 * gates its periodic CAN traffic and DTC engine on these flags. */
static bool g_comm_enabled = true;
static uint8_t g_fingerprint[4]; /* programming date (BCD) + tester id */
static bool g_fingerprint_set;

static uint32_t ecu_time(void)
{
    return g_time;
}

static int ecu_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
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

/* CommunicationControl (0x28): a real ECU stops/starts transmitting its
 * periodic application messages here so they don't collide with the flash. */
static int ecu_comm_control(uds_ctx_t *ctx, uint8_t ctrl_type, uint8_t comm_type, uint16_t node_id)
{
    (void) ctx;
    (void) comm_type;
    (void) node_id;
    /* 0x00 = enableRxAndTx, 0x03 = disableRxAndTx (ISO 14229-1 Table 26). */
    g_comm_enabled = (ctrl_type == 0x00u);
    printf("    [ECU] normal CAN messages %s\n", g_comm_enabled ? "ENABLED" : "DISABLED");
    return 0;
}

/* WriteDataByIdentifier (0x2E) callback for the programming fingerprint DID.
 * The tester records who flashed the ECU and when, before erasing anything. */
static int ecu_write_fingerprint(uds_ctx_t *ctx, uint16_t did, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) did;
    if (len != sizeof(g_fingerprint)) {
        return -0x13; /* incorrectMessageLengthOrInvalidFormat */
    }
    memcpy(g_fingerprint, data, sizeof(g_fingerprint));
    g_fingerprint_set = true;
    printf("    [ECU] fingerprint stored: %02X%02X%02X tester=0x%02X\n", g_fingerprint[0],
           g_fingerprint[1], g_fingerprint[2], g_fingerprint[3]);
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

static int ecu_transfer(uds_ctx_t *ctx, uint8_t sequence, const uint8_t *data, uint16_t len)
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

static int ecu_routine(uds_ctx_t *ctx, uint8_t type, uint16_t id, const uint8_t *data, uint16_t len,
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

/* ECUReset (0x11): on real silicon this calls NVIC_SystemReset() and never
 * returns, rebooting into the freshly written application. In this single
 * process we only log it - the library has already put the positive response
 * on the wire by the time this hook runs. */
static void ecu_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    printf("    [ECU] reset type 0x%02X - rebooting into new application\n", type);
}

/* Programming fingerprint DID: ISO 14229-1 reserves 0xF15A for the
 * "ECU programming fingerprint" (date + tester serial). Written in the
 * programming session right before the erase. */
static const uds_did_entry_t g_ecu_dids[] = {
    {0xF15Au, sizeof(g_fingerprint), 0u, 0u, NULL, ecu_write_fingerprint, NULL},
};

static const uds_did_table_t g_ecu_did_table = {
    .entries = g_ecu_dids,
    .count = sizeof(g_ecu_dids) / sizeof(g_ecu_dids[0]),
};

/* --- Flash tool driver --- */
static uds_ctx_t g_ecu;

/* Send one request to the ECU and return its captured response. */
static const uint8_t *tool_send(const uint8_t *req, uint16_t len, uint16_t *resp_len)
{
    g_resp_len = 0u;
    uds_input_sdu(&g_ecu, req, len);
    *resp_len = g_resp_len;
    return g_resp;
}

static int step(int n, const char *name, const uint8_t *req, uint16_t len)
{
    uint16_t rl;
    const uint8_t *r = tool_send(req, len, &rl);
    if (rl >= 1u && r[0] == (uint8_t) (req[0] + 0x40u)) {
        printf("[%2d] %-30s OK\n", n, name);
        return 0;
    }
    printf("[%2d] %-30s FAILED ->", n, name);
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
    cfg.fn_comm_control = ecu_comm_control;
    cfg.did_table = g_ecu_did_table;
    cfg.fn_request_download = ecu_request_download;
    cfg.fn_transfer_data = ecu_transfer;
    cfg.fn_transfer_exit = ecu_transfer_exit;
    cfg.fn_routine_control = ecu_routine;
    cfg.fn_reset = ecu_reset;
    uds_init(&g_ecu, &cfg);

    printf("=== LibUDS ECU reprogramming demo - 17-step flash sequence ===\n");
    int fail = 0;
    uint16_t rl;
    const uint8_t *r;

    /* --- Pre-programming: default -> extended, silence faults and bus --- */

    /* 1. Switch out of the default session into the extended session. */
    uint8_t ext_sess[] = {0x10, 0x03};
    fail |= step(1, "DiagnosticSession(extended)", ext_sess, sizeof(ext_sess));

    /* 2. Turn DTC logging OFF so the disruption of flashing is not recorded. */
    uint8_t dtc_off[] = {0x85, 0x02};
    fail |= step(2, "ControlDTCSetting(off)", dtc_off, sizeof(dtc_off));

    /* 3. Disable Rx/Tx of normal application messages (commType 0x01). */
    uint8_t comm_off[] = {0x28, 0x03, 0x01};
    fail |= step(3, "CommunicationControl(disable)", comm_off, sizeof(comm_off));

    /* --- Programming session + security unlock --- */

    /* 4. Enter the programming session; reprogramming services unlock here. */
    uint8_t prog_sess[] = {0x10, 0x02};
    fail |= step(4, "DiagnosticSession(programming)", prog_sess, sizeof(prog_sess));

    /* 5/6. Security access: request seed, answer with key = seed ^ 0xFF. */
    uint8_t req_seed[] = {0x27, 0x01};
    r = tool_send(req_seed, sizeof(req_seed), &rl);
    if (rl >= 6u && r[0] == 0x67u) {
        printf("[ 5] %-30s OK seed=%02X%02X%02X%02X\n", "SecurityAccess(seed)", r[2], r[3], r[4],
               r[5]);
        uint8_t key[] = {0x27,
                         0x02,
                         (uint8_t) (r[2] ^ 0xFFu),
                         (uint8_t) (r[3] ^ 0xFFu),
                         (uint8_t) (r[4] ^ 0xFFu),
                         (uint8_t) (r[5] ^ 0xFFu)};
        fail |= step(6, "SecurityAccess(key)", key, sizeof(key));
    }
    else {
        printf("[ 5] %-30s FAILED\n", "SecurityAccess(seed)");
        fail = 1;
    }

    /* 7. Write the programming fingerprint (date 26-06-27 + tester id 0x01). */
    uint8_t fingerprint[] = {0x2E, 0xF1, 0x5A, 0x26, 0x06, 0x27, 0x01};
    fail |= step(7, "WriteDataByID(fingerprint)", fingerprint, sizeof(fingerprint));

    /* --- Download the new application image --- */

    /* 8. Erase the application flash region (routine 0xFF00). */
    uint8_t erase[] = {0x31, 0x01, 0xFF, 0x00};
    fail |= step(8, "RoutineControl(erase)", erase, sizeof(erase));

    /* 9. Request download: addr 0x08000000, size 16, ALFID 0x44. */
    uint8_t dl[] = {0x34, 0x00, 0x44, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
    fail |= step(9, "RequestDownload", dl, sizeof(dl));

    /* 10/11. Transfer two 8-byte blocks. */
    uint8_t blk1[] = {0x36, 0x01, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t blk2[] = {0x36, 0x02, 9, 10, 11, 12, 13, 14, 15, 16};
    fail |= step(10, "TransferData #1", blk1, sizeof(blk1));
    fail |= step(11, "TransferData #2", blk2, sizeof(blk2));

    /* 12. Tell the ECU the transfer is finished. */
    uint8_t exit_req[] = {0x37};
    fail |= step(12, "RequestTransferExit", exit_req, sizeof(exit_req));

    /* 13. Verify the image: checksum / programming-dependency routine 0x0202. */
    uint8_t checksum[] = {0x31, 0x01, 0x02, 0x02};
    r = tool_send(checksum, sizeof(checksum), &rl);
    if (rl >= 5u && r[0] == 0x71u) {
        printf("[13] %-30s OK checksum=0x%02X\n", "RoutineControl(checkMemory)", r[4]);
    }
    else {
        printf("[13] %-30s FAILED\n", "RoutineControl(checkMemory)");
        fail = 1;
    }

    /* --- Finalize: activate the new app and restore normal operation --- */

    /* 14. Hard reset: the ECU reboots into the application just written. */
    uint8_t reset[] = {0x11, 0x01};
    fail |= step(14, "ECUReset(hardReset)", reset, sizeof(reset));

    /* 15. Back to the extended session after the (simulated) reboot. */
    fail |= step(15, "DiagnosticSession(extended)", ext_sess, sizeof(ext_sess));

    /* 16. Re-enable Rx/Tx of normal application messages. */
    uint8_t comm_on[] = {0x28, 0x00, 0x01};
    fail |= step(16, "CommunicationControl(enable)", comm_on, sizeof(comm_on));

    /* 17. Turn DTC logging back ON. */
    uint8_t dtc_on[] = {0x85, 0x01};
    fail |= step(17, "ControlDTCSetting(on)", dtc_on, sizeof(dtc_on));

    /* Post-conditions: the side effects the application must observe once the
     * sequence completes - the fingerprint was recorded and the bus is live. */
    if (!g_fingerprint_set) {
        printf("[!!] fingerprint was never recorded\n");
        fail = 1;
    }
    if (!g_comm_enabled) {
        printf("[!!] normal CAN messages left disabled\n");
        fail = 1;
    }

    printf("=== %s ===\n", fail ? "REPROGRAMMING FAILED" : "Reprogramming sequence complete");
    return fail;
}
