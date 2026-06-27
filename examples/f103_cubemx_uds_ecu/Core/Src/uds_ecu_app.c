/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_ecu_app.c
 * @brief STM32F103 + bxCAN UDS ECU - the on-target side of the 17-step flash.
 *
 * This is the HAL port of examples/pro_flash_tool (which runs the tester and
 * the ECU in one host process). Here the ECU lives on a real STM32F103 and
 * answers a diagnostic tester over classical CAN. The same LibUDS core and the
 * same service callbacks drive the canonical reprogramming sequence:
 *
 *   0x10 session control   0x27 security access   0x2E write fingerprint
 *   0x85 control DTC        0x28 comm control      0x31 erase / checkMemory
 *   0x34 request download   0x36 transfer data     0x37 transfer exit
 *   0x11 ECU reset
 *
 * Transport: ISO-TP (ISO 15765-2) over bxCAN, OBD-II IDs 0x7E0 (rx) / 0x7E8
 * (tx). The new application image is written into the top 8 KB of flash
 * (0x0800E000..0x0800FFFF); the running firmware is fenced below that by the
 * linker (56 KB). A production bootloader would split into separate bootloader
 * and application banks with rollback - intentionally out of scope here.
 */

#include <stdbool.h>
#include <string.h>

#include "uds_ecu_app.h"

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* OBD-II physical-addressing CAN IDs (ISO 15765-4). */
#define UDS_RX_ID 0x7E0u /* tester -> ECU */
#define UDS_TX_ID 0x7E8u /* ECU -> tester */

/* Download target: the top 8 KB of the 64 KB device, above the 56 KB the
 * linker reserves for this firmware. F103 medium-density flash pages are 1 KB. */
#define APP_REGION_ADDR 0x0800E000u
#define APP_REGION_SIZE 0x2000u
#define FLASH_PAGE_BYTES 0x400u

/* --- Peripheral handles, bound in uds_app_init() --- */
static CAN_HandleTypeDef *g_hcan;
static CRC_HandleTypeDef *g_hcrc;

/* --- UDS / ISO-TP state --- */
static uds_ctx_t g_uds;
static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_tx_sdu[1100];
static uint8_t g_uds_rx[1100];
static uint8_t g_uds_tx[1100];

/* --- Reprogramming state --- */
static uint32_t g_prog_addr;     /* next flash write address */
static uint32_t g_prog_len;      /* bytes written so far */
static uint8_t g_fingerprint[4]; /* programming date (BCD) + tester id */
static bool g_fingerprint_set;

/* ---- transport ---------------------------------------------------------- */

static uint32_t app_time_ms(void)
{
    return HAL_GetTick();
}

/* Low-level CAN frame TX used by the ISO-TP layer. */
static int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_TxHeaderTypeDef tx = {0};
    uint32_t mailbox;
    tx.StdId = id;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = len;
    if (HAL_CAN_AddTxMessage(g_hcan, &tx, (uint8_t *) data, &mailbox) != HAL_OK) {
        return -1;
    }
    return 0;
}

/* UDS server -> transport adapter (config.fn_tp_send). */
static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    return uds_isotp_send(&g_iso, data, len);
}

/* ---- service callbacks (mirror examples/pro_flash_tool) ----------------- */

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

/* Demo scheme only: key = seed XOR 0xFF. A real ECU runs AES-CMAC or similar
 * (see examples/security_access_mbedtls). */
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

/* 0x28: stop/start the ECU's periodic application CAN traffic. */
static int ecu_comm_control(uds_ctx_t *ctx, uint8_t ctrl_type, uint8_t comm_type, uint16_t node_id)
{
    (void) ctx;
    (void) comm_type;
    (void) node_id;
    /* 0x00 enableRxAndTx, 0x03 disableRxAndTx (ISO 14229-1 Table 26). A real
     * lamp ECU would gate its status-frame transmitter here. */
    (void) ctrl_type;
    return 0;
}

/* 0x2E DID 0xF15A: record the programming fingerprint before erasing. */
static int ecu_write_fingerprint(uds_ctx_t *ctx, uint16_t did, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) did;
    if (len != sizeof(g_fingerprint)) {
        return -0x13; /* incorrectMessageLengthOrInvalidFormat */
    }
    memcpy(g_fingerprint, data, sizeof(g_fingerprint));
    g_fingerprint_set = true;
    return 0;
}

/* 0x34: validate the request and arm the half-word programmer. */
static int ecu_request_download(uds_ctx_t *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    if (addr < APP_REGION_ADDR || (addr + size) > (APP_REGION_ADDR + APP_REGION_SIZE)) {
        return -0x31; /* requestOutOfRange */
    }
    if (!g_fingerprint_set) {
        return -0x22; /* conditionsNotCorrect: no fingerprint stamped */
    }
    g_prog_addr = addr;
    g_prog_len = 0;
    return 0;
}

/* 0x36: program each received block into flash as 16-bit half-words. */
static int ecu_transfer(uds_ctx_t *ctx, uint8_t sequence, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) sequence;
    if ((g_prog_addr + len) > (APP_REGION_ADDR + APP_REGION_SIZE)) {
        return -0x71; /* transferDataSuspended */
    }
    HAL_FLASH_Unlock();
    for (uint16_t i = 0; i < len; i += 2u) {
        uint16_t hw = data[i];
        if ((uint16_t) (i + 1u) < len) {
            hw |= (uint16_t) ((uint16_t) data[i + 1u] << 8);
        }
        else {
            hw |= 0xFF00u; /* pad odd tail with erased value */
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, g_prog_addr, hw) != HAL_OK) {
            HAL_FLASH_Lock();
            return -0x72; /* generalProgrammingFailure */
        }
        g_prog_addr += 2u;
        g_prog_len += 2u;
    }
    HAL_FLASH_Lock();
    return 0;
}

static int ecu_transfer_exit(uds_ctx_t *ctx)
{
    (void) ctx;
    return 0;
}

/* 0x31: 0xFF00 erase the app region, 0x0202 CRC-check the written image. */
static int ecu_routine(uds_ctx_t *ctx, uint8_t type, uint16_t id, const uint8_t *data, uint16_t len,
                       uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) type;
    (void) data;
    (void) len;

    if (id == 0xFF00u) { /* eraseMemory */
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0;
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = APP_REGION_ADDR;
        erase.NbPages = APP_REGION_SIZE / FLASH_PAGE_BYTES;
        HAL_FLASH_Unlock();
        HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &page_error);
        HAL_FLASH_Lock();
        if (st != HAL_OK) {
            return -0x72; /* generalProgrammingFailure */
        }
        g_prog_addr = APP_REGION_ADDR;
        g_prog_len = 0;
        return 0;
    }

    if (id == 0x0202u) { /* checkMemory: CRC-32 over the written image */
        if (max_len < 4u || g_prog_len == 0u) {
            return -0x31; /* requestOutOfRange */
        }
        uint32_t words = (g_prog_len + 3u) / 4u;
        uint32_t crc = HAL_CRC_Calculate(g_hcrc, (uint32_t *) APP_REGION_ADDR, words);
        out_buf[0] = (uint8_t) (crc >> 24);
        out_buf[1] = (uint8_t) (crc >> 16);
        out_buf[2] = (uint8_t) (crc >> 8);
        out_buf[3] = (uint8_t) crc;
        return 4;
    }

    return -0x31; /* requestOutOfRange */
}

/* Reports whether the last response has physically left the bus (all three TX
 * mailboxes empty). The library gates the deferred ECUReset on this hook. */
static bool ecu_tx_complete(uds_ctx_t *ctx)
{
    (void) ctx;
    return HAL_CAN_GetTxMailboxesFreeLevel(g_hcan) == 3u;
}

/* 0x11: the library calls this only once ecu_tx_complete() confirms the 0x51
 * response is on the wire (or reset_tx_wait_ms elapses), so it is safe to reboot
 * here - on real silicon NVIC_SystemReset never returns. */
static void ecu_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    HAL_NVIC_SystemReset();
}

/* ---- public API --------------------------------------------------------- */

/* Programming fingerprint DID (ISO 14229-1 reserves 0xF15A). */
static const uds_did_entry_t g_dids[] = {
    {0xF15Au, sizeof(g_fingerprint), 0u, 0u, NULL, ecu_write_fingerprint, NULL},
};

void uds_app_init(CAN_HandleTypeDef *hcan, CRC_HandleTypeDef *hcrc)
{
    g_hcan = hcan;
    g_hcrc = hcrc;

    /* Accept only the tester->ECU request ID into FIFO0 (32-bit mask mode). */
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t) (UDS_RX_ID << 5);
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = (uint16_t) (0x7FFu << 5);
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    HAL_CAN_ConfigFilter(g_hcan, &filter);
    HAL_CAN_Start(g_hcan);

    /* uds_init() keeps a pointer to this config (it does not copy it), so the
     * struct must outlive uds_app_init() - hence static, not a stack local. */
    static uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = app_time_ms;
    cfg.fn_tp_send = isotp_send_adapter;
    cfg.rx_buffer = g_uds_rx;
    cfg.rx_buffer_size = sizeof(g_uds_rx);
    cfg.tx_buffer = g_uds_tx;
    cfg.tx_buffer_size = sizeof(g_uds_tx);
    cfg.p2_ms = 50;
    cfg.p2_star_ms = 5000;
    cfg.restrict_sessions = true; /* reprogramming services gated to programming session */
    cfg.fn_security_seed = ecu_seed;
    cfg.fn_security_key = ecu_key;
    cfg.fn_comm_control = ecu_comm_control;
    cfg.did_table.entries = g_dids;
    cfg.did_table.count = sizeof(g_dids) / sizeof(g_dids[0]);
    cfg.fn_request_download = ecu_request_download;
    cfg.fn_transfer_data = ecu_transfer;
    cfg.fn_transfer_exit = ecu_transfer_exit;
    cfg.fn_routine_control = ecu_routine;
    cfg.fn_reset = ecu_reset;
    cfg.fn_tx_complete = ecu_tx_complete; /* hold the reset until 0x51 is on the wire */
    uds_init(&g_uds, &cfg);

    uds_tp_isotp_init(&g_iso, can_send, UDS_TX_ID, UDS_RX_ID, g_iso_tx_sdu, sizeof(g_iso_tx_sdu));
}

void uds_app_poll(void)
{
    CAN_RxHeaderTypeDef rx;
    uint8_t frame[8];

    while (HAL_CAN_GetRxFifoFillLevel(g_hcan, CAN_RX_FIFO0) > 0u) {
        if (HAL_CAN_GetRxMessage(g_hcan, CAN_RX_FIFO0, &rx, frame) == HAL_OK) {
            uds_isotp_rx_callback(&g_iso, &g_uds, rx.StdId, frame, (uint8_t) rx.DLC);
        }
    }

    /* uds_process() drives time-based server work: P2-star / RCRRP pending
     * responses and the deferred post-TX ECUReset (fired via ecu_tx_complete). */
    uds_process(&g_uds);
    uds_tp_isotp_process(&g_iso, HAL_GetTick());
}
