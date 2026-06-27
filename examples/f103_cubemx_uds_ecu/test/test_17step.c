/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/* Functional test for examples/f103_cubemx_uds_ecu: compiles the real firmware
 * source (uds_ecu_app.c) against a host HAL shim and drives all 17 reprogramming
 * steps over a real ISO-TP tester across a virtual CAN bus. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h" /* shim */
#include "uds_ecu_app.h"

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* shim-declared globals */
uint8_t g_shim_flash[SHIM_FLASH_SIZE];
shim_fifo_t g_ecu_tx;
shim_fifo_t g_ecu_rx;
uint32_t g_shim_tick;
int g_shim_reset_called;

/* --- tester side --- */
static uds_ctx_t t_uds;
static uds_isotp_ctx_t t_iso;
static uint8_t t_isotx[256];
static uint8_t t_rxb[256];
static uint8_t t_txb[256];

static uint8_t g_resp[256];
static uint16_t g_resp_len;
static int g_have_resp;

static uint32_t t_time(void)
{
    return g_shim_tick;
}
static int t_dummy_send(struct uds_ctx *c, const uint8_t *d, uint16_t l)
{
    (void) c;
    (void) d;
    (void) l;
    return 0;
}

/* tester's link layer: push frames to the ECU's RX queue */
static int t_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    return shim_push(&g_ecu_rx, id, data, len);
}

static void t_on_sdu(void *cookie, const uint8_t *sdu, uint16_t len, uint8_t addr)
{
    (void) cookie;
    (void) addr;
    memcpy(g_resp, sdu, len);
    g_resp_len = len;
    g_have_resp = 1;
}

static int fails;

/* Send one request and pump both stacks until the response is reassembled. */
static const uint8_t *send_step(const char *name, int n, const uint8_t *req, uint16_t len,
                                uint16_t *out_len)
{
    g_have_resp = 0;
    g_resp_len = 0;
    uds_isotp_send(&t_iso, req, len);
    for (int i = 0; i < 5000 && !g_have_resp; i++) {
        g_shim_tick++;
        uds_app_poll(); /* ECU: drain rx, serve, pump its isotp */
        shim_frame_t fr;
        while (shim_pop(&g_ecu_tx, &fr) == 0) {
            uds_isotp_rx_callback(&t_iso, &t_uds, fr.id, fr.data, fr.len);
        }
        uds_tp_isotp_process(&t_iso, g_shim_tick);
    }
    *out_len = g_resp_len;
    if (!g_have_resp) {
        printf("[%2d] %-30s NO RESPONSE\n", n, name);
        fails++;
        return g_resp;
    }
    if (g_resp[0] == (uint8_t) (req[0] + 0x40u)) {
        printf("[%2d] %-30s OK  ->", n, name);
    }
    else {
        printf("[%2d] %-30s FAIL->", n, name);
        fails++;
    }
    for (uint16_t i = 0; i < g_resp_len && i < 10; i++) printf(" %02X", g_resp[i]);
    printf("\n");
    return g_resp;
}

int main(void)
{
    CAN_HandleTypeDef hcan = {0};
    CRC_HandleTypeDef hcrc = {0};

    /* Pre-fill the flash window with junk so erase is observable. */
    memset(g_shim_flash, 0x5A, sizeof(g_shim_flash));

    /* ECU under test (the real firmware code). */
    uds_app_init(&hcan, &hcrc);

    /* Tester ISO-TP: tx 0x7E0 (to ECU), rx 0x7E8 (from ECU). */
    uds_config_t tc;
    memset(&tc, 0, sizeof(tc));
    tc.get_time_ms = t_time;
    tc.fn_tp_send = t_dummy_send;
    tc.rx_buffer = t_rxb;
    tc.rx_buffer_size = sizeof(t_rxb);
    tc.tx_buffer = t_txb;
    tc.tx_buffer_size = sizeof(t_txb);
    tc.p2_ms = 50;
    tc.p2_star_ms = 5000;
    uds_init(&t_uds, &tc);
    uds_tp_isotp_init(&t_iso, t_can_send, 0x7E0u, 0x7E8u, t_isotx, sizeof(t_isotx));
    uds_isotp_set_sdu_handler(&t_iso, t_on_sdu, NULL);

    printf("=== F103 CubeMX UDS ECU - 17-step reprogramming over ISO-TP/bxCAN ===\n");
    uint16_t rl;
    const uint8_t *r;

    uint8_t ext[] = {0x10, 0x03};
    send_step("DiagnosticSession(extended)", 1, ext, sizeof(ext), &rl);
    uint8_t dtc_off[] = {0x85, 0x02};
    send_step("ControlDTCSetting(off)", 2, dtc_off, sizeof(dtc_off), &rl);
    uint8_t comm_off[] = {0x28, 0x03, 0x01};
    send_step("CommunicationControl(disable)", 3, comm_off, sizeof(comm_off), &rl);
    uint8_t prog[] = {0x10, 0x02};
    send_step("DiagnosticSession(programming)", 4, prog, sizeof(prog), &rl);

    uint8_t seed[] = {0x27, 0x01};
    r = send_step("SecurityAccess(seed)", 5, seed, sizeof(seed), &rl);
    uint8_t key[] = {0x27,
                     0x02,
                     (uint8_t) (r[2] ^ 0xFF),
                     (uint8_t) (r[3] ^ 0xFF),
                     (uint8_t) (r[4] ^ 0xFF),
                     (uint8_t) (r[5] ^ 0xFF)};
    send_step("SecurityAccess(key)", 6, key, sizeof(key), &rl);

    uint8_t fp[] = {0x2E, 0xF1, 0x5A, 0x26, 0x06, 0x27, 0x01};
    send_step("WriteDataByID(fingerprint)", 7, fp, sizeof(fp), &rl);

    uint8_t erase[] = {0x31, 0x01, 0xFF, 0x00};
    send_step("RoutineControl(erase)", 8, erase, sizeof(erase), &rl);
    int erased_ok = (g_shim_flash[0] == 0xFF && g_shim_flash[15] == 0xFF);

    uint8_t dl[] = {0x34, 0x00, 0x44, 0x08, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x10};
    send_step("RequestDownload", 9, dl, sizeof(dl), &rl);

    uint8_t b1[] = {0x36, 0x01, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b2[] = {0x36, 0x02, 9, 10, 11, 12, 13, 14, 15, 16};
    send_step("TransferData #1", 10, b1, sizeof(b1), &rl);
    send_step("TransferData #2", 11, b2, sizeof(b2), &rl);
    int prog_ok = 1;
    for (int i = 0; i < 16; i++)
        if (g_shim_flash[i] != (uint8_t) (i + 1)) prog_ok = 0;

    uint8_t exit_[] = {0x37};
    send_step("RequestTransferExit", 12, exit_, sizeof(exit_), &rl);

    uint8_t chk[] = {0x31, 0x01, 0x02, 0x02};
    r = send_step("RoutineControl(checkMemory)", 13, chk, sizeof(chk), &rl);
    int crc_ok = (rl == 8); /* 71 01 02 02 + 4 CRC bytes (multi-frame ISO-TP) */
    uint8_t crc_b[4] = {r[4], r[5], r[6], r[7]};

    uint8_t reset[] = {0x11, 0x01};
    send_step("ECUReset(hardReset)", 14, reset, sizeof(reset), &rl);
    int reset_ok = (g_shim_reset_called == 1);

    send_step("DiagnosticSession(extended)", 15, ext, sizeof(ext), &rl);
    uint8_t comm_on[] = {0x28, 0x00, 0x01};
    send_step("CommunicationControl(enable)", 16, comm_on, sizeof(comm_on), &rl);
    uint8_t dtc_on[] = {0x85, 0x01};
    send_step("ControlDTCSetting(on)", 17, dtc_on, sizeof(dtc_on), &rl);

    printf("--- side effects ---\n");
    printf("  erase wiped flash window : %s\n", erased_ok ? "yes" : "NO");
    printf("  16 bytes programmed      : %s\n", prog_ok ? "yes" : "NO");
    printf("  checkMemory returned CRC : %s (crc=%02X%02X%02X%02X)\n", crc_ok ? "yes" : "NO",
           crc_b[0], crc_b[1], crc_b[2], crc_b[3]);
    printf("  ECUReset fired NVIC reset: %s\n", reset_ok ? "yes" : "NO");
    if (!erased_ok || !prog_ok || !crc_ok || !reset_ok) fails++;

    printf("=== %s (%d failures) ===\n", fails ? "FAILED" : "ALL 17 STEPS PASSED", fails);
    return fails ? 1 : 0;
}
