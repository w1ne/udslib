/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS OTA Bootloader — server wiring (Task 3b)
 *
 * Boot flow:
 *   1. Print "BL-START" on UART.
 *   2. TODO (Task 4): check inactive-bank validity footer; jump to app if valid.
 *   3. Start FDCAN loopback, init ISO-TP FD, configure UDS server.
 *   4. Run polling loop: pump RX → uds_process → uds_tp_isotp_process.
 *
 * UDS server features wired here:
 *   - restrict_sessions=true: reprogramming services gated to programming
 *     session (SID 0x10 sub 0x02).
 *   - SID 0x27 security access via AES-128-CMAC (demo key — see DEMO_SECRET).
 *   - SID 0x34 RequestDownload: validates inactive-bank app region, erases sectors.
 *   - SID 0x36 TransferData: programs 16-byte-aligned chunks via flash_h5.
 *   - SID 0x37 RequestTransferExit: flushes the staging buffer.
 *   - SID 0x31 RoutineControl:
 *       0xFF00 EraseMemory       — erase the inactive-bank app sectors (alt path).
 *       0xFF01 CheckProgramming  — CRC-32 over written image vs validity footer.
 *       0xFF02 ActivateSoftware  — flash_set_swap_and_reset() (does not return).
 *
 * Flash layout (dual-bank, 1 MB per bank, 8 KB sectors):
 *   Bank 0 base: 0x08000000   Bank 1 base: 0x08100000
 *   Bootloader:  sectors 0-11 (0x00000–0x17FFF, 96 KB)
 *   App region:  sectors 12-127 (0x18000–0xFFFFF, 928 KB)
 *
 * Inactive-bank app base = bank_base + 0x18000
 * (bank_base = 0x08000000 if inactive==0, 0x08100000 if inactive==1)
 *
 * Validity footer (last 8 bytes of the image):
 *   [0..3]  MAGIC  0xC0DEBEEF (big-endian)
 *   [4..7]  CRC-32 over bytes [0 .. image_size-8] (big-endian, poly 0x04C11DB7,
 *           init 0xFFFFFFFF, reflected in/out, xor-out 0xFFFFFFFF — standard CRC-32)
 *
 * 16-byte staging buffer for TransferData:
 *   The H5 flash controller requires 16-byte (quad-word) aligned writes.
 *   fn_transfer_data accumulates data in g_stage[] and flushes on full block or
 *   on fn_transfer_exit (padded with 0xFF to 16 bytes).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fdcan.h"
#include "flash_h5.h"
#include "sec_cmac.h"
#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* ---------------------------------------------------------------------------
 * Timing (simple free-running counter — no SysTick wired; incremented in loop)
 * ------------------------------------------------------------------------- */
static volatile uint32_t g_now_ms;

static uint32_t get_time_ms(void)
{
    return g_now_ms;
}

/* ---------------------------------------------------------------------------
 * CAN / ISO-TP
 * ------------------------------------------------------------------------- */
#define BL_TX_ID 0x7E8u
#define BL_RX_ID 0x7E0u

static uds_isotp_ctx_t g_isotp;
static uint8_t g_isotp_tx_sdu[512];

static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    return uds_isotp_send(&g_isotp, data, len);
}

/* ---------------------------------------------------------------------------
 * Security Access 0x27 — AES-128-CMAC
 *
 * DEMO_SECRET is a fixed 16-byte example key only.  On a production ECU this
 * must NOT reside in flash: replace with a key handle into the HSM/SHE, making
 * aes_cmac() an HSM call.  A fixed seed is used for deterministic simulation;
 * production firmware MUST use a TRNG-derived per-attempt nonce.
 * ------------------------------------------------------------------------- */
#define SEC_SEED_LEN 16u
#define SEC_KEY_LEN  16u

/* DEMO_SECRET — example key only; MUST be replaced before production use. */
static const uint8_t DEMO_SECRET[16] = {
    0xA3, 0xF1, 0x7C, 0x28, 0xB6, 0x4E, 0xD9, 0x05,
    0x71, 0xCC, 0x3A, 0x8F, 0x52, 0x0B, 0xE4, 0x96
};

/* Fixed demo seed — replace with TRNG output in production. */
static const uint8_t DEMO_SEED[SEC_SEED_LEN] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x91, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7, 0x08
};

/* Constant-time comparison to avoid leaking how many bytes matched. */
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0u;
    for (size_t i = 0u; i < len; i++) {
        diff |= (uint8_t) (a[i] ^ b[i]);
    }
    return diff == 0;
}

static int bl_security_seed(uds_ctx_t *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len)
{
    (void) ctx;
    (void) level;
    if (max_len < SEC_SEED_LEN) {
        return -(int) 0x22; /* conditionsNotCorrect */
    }
    memcpy(seed_buf, DEMO_SEED, SEC_SEED_LEN);
    return (int) SEC_SEED_LEN;
}

static int bl_security_key(uds_ctx_t *ctx, uint8_t level, const uint8_t *seed, const uint8_t *key,
                           uint16_t key_len)
{
    (void) ctx;
    (void) level;
    uint8_t expected[SEC_KEY_LEN];

    /*
     * Key length is fixed and non-secret (part of the UDS protocol spec), so
     * returning early here without constant-time delay is acceptable — only the
     * key *bytes* are compared constant-time below.
     */
    if (key_len != SEC_KEY_LEN) {
        return -(int) 0x35; /* invalidKey: wrong length */
    }
    if (aes_cmac(DEMO_SECRET, seed, SEC_SEED_LEN, expected) != 0) {
        return -(int) 0x22; /* conditionsNotCorrect: crypto failure */
    }
    if (!ct_equal(key, expected, SEC_KEY_LEN)) {
        return -(int) 0x35; /* invalidKey */
    }
    return 0; /* grant security level */
}

/* ---------------------------------------------------------------------------
 * Flash / reprogramming state
 * ------------------------------------------------------------------------- */

/* Bank geometry */
#define BANK_SIZE        0x100000UL   /* 1 MB per bank */
#define BL_REGION_SIZE   0x18000UL    /* 96 KB bootloader (sectors 0-11) */
#define SECTOR_SIZE      0x2000UL     /* 8 KB per sector */
#define SECTORS_PER_BANK 128u         /* 1 MB / 8 KB */

/* First app sector index within a bank */
#define APP_SECTOR_FIRST 12u
/* Last app sector index within a bank (inclusive) */
#define APP_SECTOR_LAST  (SECTORS_PER_BANK - 1u)

/*
 * Validity footer (last 8 bytes of the flash image):
 *   bytes [0..3]: magic 0xC0DEBEEF (big-endian)
 *   bytes [4..7]: CRC-32 of the image (big-endian, standard CRC-32 poly)
 */
#define VALIDITY_MAGIC    0xC0DEBEEFul
#define VALIDITY_FOOTER_SZ 8u

/* 16-byte staging buffer — H5 requires quad-word aligned program operations */
#define STAGE_SZ 16u

static struct {
    uint8_t  inactive_bank;   /* 0 = bank1, 1 = bank2 */
    uint32_t app_base;        /* first writable byte of inactive app region */
    uint32_t app_end;         /* one past last writable byte */
    uint32_t dl_addr;         /* RequestDownload target base */
    uint32_t dl_size;         /* RequestDownload declared size */
    uint32_t write_cursor;    /* next flash address to program */
    uint32_t bytes_written;   /* total bytes passed to flash (pre-padding) */
    uint8_t  stage[STAGE_SZ]; /* 16-byte alignment staging buffer */
    uint8_t  stage_used;      /* bytes currently in staging buffer */
    bool     dl_active;       /* true between RequestDownload and TransferExit */
} g_flash_state;

/* Return bank base address for bank index (0 or 1). */
static uint32_t bank_base(uint8_t bank)
{
    return 0x08000000UL + (uint32_t) bank * BANK_SIZE;
}

/* Flush any accumulated bytes in g_flash_state.stage to flash (pad with 0xFF). */
static int flush_stage(void)
{
    if (g_flash_state.stage_used == 0u) {
        return 0;
    }
    /* Pad the partial block with 0xFF (erased state). */
    memset(&g_flash_state.stage[g_flash_state.stage_used], 0xFF,
           STAGE_SZ - g_flash_state.stage_used);
    int rc = flash_program(g_flash_state.write_cursor, g_flash_state.stage, STAGE_SZ);
    g_flash_state.write_cursor += STAGE_SZ;
    g_flash_state.stage_used = 0u;
    return rc;
}

/* Erase the app sectors of `bank`. */
static int erase_app_sectors(uint8_t bank)
{
    flash_unlock();
    for (uint32_t s = APP_SECTOR_FIRST; s <= APP_SECTOR_LAST; s++) {
        int rc = flash_erase_sector(bank, s);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * CRC-32 (standard: poly 0x04C11DB7, reflected in/out, init/xor 0xFFFFFFFF)
 * Software table-less (Sarwate — compact for a bootloader)
 * ------------------------------------------------------------------------- */
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    static const uint32_t poly = 0xEDB88320UL; /* reflected poly */
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= (uint32_t) buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1u) ^ poly;
            } else {
                crc >>= 1u;
            }
        }
    }
    return crc;
}

static uint32_t crc32(const uint8_t *buf, uint32_t len)
{
    return crc32_update(0xFFFFFFFFUL, buf, len) ^ 0xFFFFFFFFUL;
}

/* ---------------------------------------------------------------------------
 * UDS flash-service callbacks
 * ------------------------------------------------------------------------- */

/*
 * fn_request_download — SID 0x34
 *
 * Accept only if [addr, addr+size) lies entirely within the inactive bank's
 * app region (0x18000–0xFFFFF offset from bank base).  Reject any write into
 * the bootloader region or the active bank.
 *
 * Erases the inactive app sectors here (RequestDownload path; 0xFF00 routine
 * is an alternative — both are safe to call; the second erase is a no-op on
 * already-erased flash).
 */
static int bl_request_download(uds_ctx_t *ctx, uint32_t addr, uint32_t size)
{
    /* Security gate: 0x27 must have been completed before reprogramming. */
    if (ctx->security_level < 1u) {
        return -(int) 0x33; /* securityAccessDenied */
    }

    uint8_t inactive = flash_active_bank() ? 0u : 1u;
    uint32_t base    = bank_base(inactive) + BL_REGION_SIZE;
    uint32_t end_off = bank_base(inactive) + BANK_SIZE;

    /*
     * Overflow-safe bounds check: verify size against the fixed region span
     * BEFORE the addition so a crafted size cannot wrap uint32_t and bypass
     * the guard.  end_off - base is the app-region span, computed without
     * overflow.
     */
    if (size == 0u || size > (end_off - base) || addr < base || (addr + size) > end_off) {
        uart_puts("BL: RD reject out-of-range\n");
        return -(int) 0x70; /* uploadDownloadNotAccepted */
    }

    /* Erase inactive app region. */
    uart_puts("BL: erasing inactive app\n");
    if (erase_app_sectors(inactive) != 0) {
        return -(int) 0x70;
    }

    /* Arm the transfer state. */
    g_flash_state.inactive_bank = inactive;
    g_flash_state.app_base      = base;
    g_flash_state.app_end       = end_off;
    g_flash_state.dl_addr       = addr;
    g_flash_state.dl_size       = size;
    g_flash_state.write_cursor  = addr;
    g_flash_state.bytes_written = 0u;
    g_flash_state.stage_used    = 0u;
    g_flash_state.dl_active     = true;

    uart_puts("BL: download armed\n");
    return UDS_OK;
}

/*
 * fn_transfer_data — SID 0x36
 *
 * Accumulate data in a 16-byte staging buffer; flush full blocks to flash
 * immediately.  Bounds-check against the declared download region.
 */
static int bl_transfer_data(uds_ctx_t *ctx, uint8_t sequence, const uint8_t *data, uint16_t len)
{
    (void) sequence;

    /* Security gate: 0x27 must have been completed before reprogramming. */
    if (ctx->security_level < 1u) {
        return -(int) 0x33; /* securityAccessDenied */
    }

    if (!g_flash_state.dl_active) {
        return -(int) 0x70;
    }

    /* Bounds check: refuse to write past the declared download region. */
    if ((g_flash_state.bytes_written + len) > g_flash_state.dl_size) {
        return -(int) 0x71; /* transferDataSuspended */
    }
    if ((g_flash_state.write_cursor + len + g_flash_state.stage_used) >
        g_flash_state.app_end) {
        return -(int) 0x71;
    }

    uint16_t pos = 0u;
    while (pos < len) {
        uint8_t space = (uint8_t) (STAGE_SZ - g_flash_state.stage_used);
        uint16_t chunk = (uint16_t) (len - pos);
        if (chunk > (uint16_t) space) {
            chunk = (uint16_t) space;
        }
        memcpy(&g_flash_state.stage[g_flash_state.stage_used], data + pos, chunk);
        g_flash_state.stage_used = (uint8_t) (g_flash_state.stage_used + chunk);
        pos = (uint16_t) (pos + chunk);

        if (g_flash_state.stage_used == STAGE_SZ) {
            int rc = flash_program(g_flash_state.write_cursor, g_flash_state.stage, STAGE_SZ);
            if (rc != 0) {
                return -(int) 0x72; /* generalProgrammingFailure */
            }
            g_flash_state.write_cursor += STAGE_SZ;
            g_flash_state.stage_used    = 0u;
        }
    }

    g_flash_state.bytes_written += len;
    return UDS_OK;
}

/*
 * fn_transfer_exit — SID 0x37
 *
 * Flush any remaining staged bytes (padded to 16 bytes with 0xFF).
 */
static int bl_transfer_exit(uds_ctx_t *ctx)
{
    /* Security gate: 0x27 must have been completed before reprogramming. */
    if (ctx->security_level < 1u) {
        return -(int) 0x33; /* securityAccessDenied */
    }

    if (!g_flash_state.dl_active) {
        return -(int) 0x70;
    }

    int rc = flush_stage();
    g_flash_state.dl_active = false;

    if (rc != 0) {
        return -(int) 0x72; /* generalProgrammingFailure */
    }
    uart_puts("BL: transfer complete\n");
    return UDS_OK;
}

/*
 * fn_routine_control — SID 0x31
 *
 * Routine IDs (matching Vector CANdela naming conventions):
 *   0xFF00  EraseMemory              — erase inactive-bank app sectors
 *   0xFF01  CheckProgrammingDependencies — CRC-32 over written image vs footer
 *   0xFF02  ActivateSoftware         — flash_set_swap_and_reset() (no return)
 */
static int bl_routine_control(uds_ctx_t *ctx, uint8_t type, uint16_t id, const uint8_t *data,
                               uint16_t len, uint8_t *out_buf, uint16_t max_len)
{
    (void) type;
    (void) data;
    (void) len;
    (void) max_len;

    /* Security gate: 0x27 must have been completed before reprogramming. */
    if (ctx->security_level < 1u) {
        return -(int) 0x33; /* securityAccessDenied */
    }

    if (id == 0xFF00u) {
        /* EraseMemory: erase inactive-bank app sectors.
         * This is an alternative to the erase done in RequestDownload; calling
         * it standalone is safe — erasing already-erased flash is a no-op on
         * the H5. */
        uint8_t inactive = flash_active_bank() ? 0u : 1u;
        uart_puts("BL: routine erase\n");
        int rc = erase_app_sectors(inactive);
        if (rc != 0) {
            return -(int) 0x72; /* generalProgrammingFailure */
        }
        /*
         * Reset only transfer-state fields; preserve inactive_bank/app_base/
         * app_end so they are not transiently zeroed between the erase and the
         * next RequestDownload.
         */
        g_flash_state.dl_active     = false;
        g_flash_state.dl_addr       = 0u;
        g_flash_state.dl_size       = 0u;
        g_flash_state.write_cursor  = 0u;
        g_flash_state.bytes_written = 0u;
        g_flash_state.stage_used    = 0u;
        memset(g_flash_state.stage, 0, sizeof(g_flash_state.stage));
        return 0;
    }

    if (id == 0xFF01u) {
        /*
         * CheckProgrammingDependencies:
         *
         * Validity footer is the last VALIDITY_FOOTER_SZ bytes of the written
         * image (at g_flash_state.dl_addr + g_flash_state.bytes_written - 8):
         *   [0..3] = MAGIC 0xC0DEBEEF (big-endian)
         *   [4..7] = CRC-32 over bytes [0 .. image_size-8] (big-endian)
         *
         * CRC is computed over the raw flash bytes starting at dl_addr.
         * Returns 1 byte result: 0x01 = PASS, 0x00 = FAIL.
         */
        if (g_flash_state.bytes_written == 0u) {
            /* No download performed yet — reject. */
            return -(int) 0x22; /* conditionsNotCorrect */
        }

        uint32_t image_size = g_flash_state.bytes_written;
        if (image_size < VALIDITY_FOOTER_SZ) {
            if (max_len < 1u) {
                return -(int) 0x14;
            }
            out_buf[0] = 0x00u; /* FAIL */
            return 1;
        }

        const uint8_t *flash_ptr = (const uint8_t *) (uintptr_t) g_flash_state.dl_addr;
        uint32_t body_len = image_size - VALIDITY_FOOTER_SZ;
        const uint8_t *footer = flash_ptr + body_len;

        /* Read magic (big-endian). */
        uint32_t magic = ((uint32_t) footer[0] << 24u) | ((uint32_t) footer[1] << 16u) |
                         ((uint32_t) footer[2] << 8u)  |  (uint32_t) footer[3];

        /* Read stored CRC (big-endian). */
        uint32_t stored_crc = ((uint32_t) footer[4] << 24u) | ((uint32_t) footer[5] << 16u) |
                              ((uint32_t) footer[6] << 8u)  |  (uint32_t) footer[7];

        /* Compute CRC over the image body. */
        uint32_t computed_crc = crc32(flash_ptr, body_len);

        uint8_t pass = (magic == VALIDITY_MAGIC && computed_crc == stored_crc) ? 0x01u : 0x00u;

        if (max_len < 1u) {
            return -(int) 0x14;
        }
        out_buf[0] = pass;
        if (pass) {
            uart_puts("BL: CRC check PASS\n");
        } else {
            uart_puts("BL: CRC check FAIL\n");
        }
        return 1;
    }

    if (id == 0xFF02u) {
        /* ActivateSoftware: swap banks and reset — does not return. */
        uart_puts("BL: activate software\n");
        flash_set_swap_and_reset();
        /* flash_set_swap_and_reset() issues a system reset; this line is unreachable. */
        for (;;) {
        }
    }

    return -(int) 0x31; /* requestOutOfRange */
}

/* ---------------------------------------------------------------------------
 * UDS context
 * ------------------------------------------------------------------------- */
static uds_ctx_t  g_uds;
static uint8_t    g_rx_buf[512];
static uint8_t    g_tx_buf[512];

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    uart_init();
    uart_puts("BL-START\n");

    /*
     * TODO (Task 4): validate inactive-bank app image (check validity footer);
     * if valid, jump to the application entry point.  Omitted here intentionally
     * to allow Task 4 to implement the boot decision logic cleanly.
     */

    /* Start FDCAN in loopback mode (self-test / simulation). */
    fdcan_start();

    /* ISO-TP FD: TX=0x7E8, RX=0x7E0 (standard OBD diagnostic IDs). */
    uds_tp_isotp_init(&g_isotp, can_send, BL_TX_ID, BL_RX_ID,
                      g_isotp_tx_sdu, (uint16_t) sizeof(g_isotp_tx_sdu));
    uds_tp_isotp_set_fd(&g_isotp, true);

    /* Configure the UDS server. */
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ecu_address      = 0x10u;
    cfg.get_time_ms      = get_time_ms;
    cfg.fn_tp_send       = isotp_send_adapter;
    cfg.p2_ms            = 50u;
    cfg.p2_star_ms       = 5000u;
    cfg.rx_buffer        = g_rx_buf;
    cfg.rx_buffer_size   = (uint16_t) sizeof(g_rx_buf);
    cfg.tx_buffer        = g_tx_buf;
    cfg.tx_buffer_size   = (uint16_t) sizeof(g_tx_buf);

    /* Gate reprogramming services (0x34/0x36/0x37/0x31/0x27) to the
     * programming session (ISO 14229-1 sensible defaults). */
    cfg.restrict_sessions = true;

    /* 0x27 Security Access via AES-128-CMAC (DEMO key — see DEMO_SECRET). */
    cfg.fn_security_seed     = bl_security_seed;
    cfg.fn_security_key      = bl_security_key;
    cfg.security_max_attempts = 3u;
    cfg.security_delay_ms    = 10000u;

    /* Flash reprogramming callbacks. */
    cfg.fn_request_download = bl_request_download;
    cfg.fn_transfer_data    = bl_transfer_data;
    cfg.fn_transfer_exit    = bl_transfer_exit;
    cfg.fn_routine_control  = bl_routine_control;

    if (uds_init(&g_uds, &cfg) != UDS_OK) {
        uart_puts("BL: uds_init FAIL\n");
        for (;;) {
        }
    }

    uart_puts("BL: UDS server ready\n");

    /* Polling loop — no NVIC IRQs. */
    for (;;) {
        /* Pump incoming CAN frames into the ISO-TP layer. */
        can_frame_t frame;
        while (fdcan_poll_rx_frame(&frame)) {
            if (frame.id == BL_RX_ID) {
                uds_isotp_rx_callback(&g_isotp, &g_uds, frame.id, frame.data, frame.len);
            }
        }

        /* Run the UDS service dispatcher. */
        uds_process(&g_uds);

        /* Advance ISO-TP timers and drive multi-frame TX. */
        uds_tp_isotp_process(&g_isotp, g_now_ms);

        /* Increment the free-running millisecond counter. */
        ++g_now_ms;
    }
}
