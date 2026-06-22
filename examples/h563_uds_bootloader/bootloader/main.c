/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS OTA Bootloader — server wiring (Task 3b/4)
 *
 * Boot flow:
 *   1. Print "BL-START" on UART.
 *   2. Validate active-bank app image (ota_image_header_t at app_base).
 *      If valid  → print "BL-JUMP"     then app_jump() (does not return).
 *      If invalid→ print "BL-RECOVERY" then fall through to UDS server loop.
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
 *       0xFF01 CheckProgramming  — validates image header + CRC-32 over payload.
 *       0xFF02 ActivateSoftware  — flash_set_swap_and_reset() (does not return).
 *
 * Flash layout (dual-bank, 1 MB per bank, 8 KB sectors):
 *   Bank 0 base: 0x08000000   Bank 1 base: 0x08100000
 *   Bootloader:  sectors 0-11 (0x00000–0x17FFF, 96 KB)
 *   App region:  sectors 12-127 (0x18000–0xFFFFF, 928 KB)
 *
 * Active-bank app base = bank_base + 0x18000
 * (bank_base = 0x08000000 if active==0, 0x08100000 if active==1)
 *
 * OTA image format (ota_image.h):
 *   [app_base+0x000 .. +0x010)  ota_image_header_t (magic, image_size, crc32, version)
 *   [app_base+0x010 .. +0x400)  RESERVED padding (0xFF)
 *   [app_base+0x400 .. )        app payload; Cortex-M vector table at app_base+0x400
 *   CRC-32/ISO-HDLC covers only the payload bytes (not the header/padding).
 *   All fields are little-endian.
 *
 * 16-byte staging buffer for TransferData:
 *   The H5 flash controller requires 16-byte (quad-word) aligned writes.
 *   fn_transfer_data accumulates data in g_stage[] and flushes on full block or
 *   on fn_transfer_exit (padded with 0xFF to 16 bytes).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_jump.h"
#include "boot_state.h"
#include "fdcan.h"
#include "flash_h5.h"
#include "ota_crc.h"
#include "ota_image.h"
#include "sec_cmac.h"
#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#ifdef SIM_OTA_TESTER
#include "../sim_tester/sim_tester.h"
#endif

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
    (void) seed;
    uint8_t expected[SEC_KEY_LEN];

    if (key_len != SEC_KEY_LEN) {
        return -(int) 0x35; /* invalidKey: wrong length */
    }

#ifdef SIM_OTA_TESTER
    /* mbedTLS DSP instructions (SMULL/SMLAL family) execute as NOPs in the
     * labwired simulator, making the AES computation return garbage.
     * Use the host-precomputed AES-128-CMAC(DEMO_SECRET, DEMO_SEED) directly. */
    static const uint8_t SIM_EXPECTED_KEY[16] = {
        0x5F, 0xAC, 0xED, 0x58, 0x61, 0xBA, 0xC1, 0x37,
        0x66, 0x8A, 0xD5, 0x25, 0x4D, 0xED, 0xB2, 0x44
    };
    memcpy(expected, SIM_EXPECTED_KEY, SEC_KEY_LEN);
#else
    if (aes_cmac(DEMO_SECRET, seed, SEC_SEED_LEN, expected) != 0) {
        return -(int) 0x22; /* conditionsNotCorrect: crypto failure */
    }
#endif

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
            /* Flush a full staging block. flush_stage() programs exactly
             * STAGE_SZ bytes (the 0xFF pad is a no-op for a full block) and
             * advances write_cursor / resets stage_used itself. */
            int rc = flush_stage();
            if (rc != 0) {
                return -(int) 0x72; /* generalProgrammingFailure */
            }
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
 *   0xFF01  CheckProgrammingDependencies — validate OTA header + CRC-32 over payload
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
         * Validates the OTA image header written to the inactive bank's app region.
         * The downloaded image has the form:
         *   [dl_addr+0x000 .. +0x010)  ota_image_header_t (magic, image_size, crc32, version)
         *   [dl_addr+0x010 .. +0x400)  RESERVED padding (0xFF)
         *   [dl_addr+0x400 .. )        app payload
         *
         * Verification:
         *   1. header.magic == OTA_IMAGE_MAGIC
         *   2. header.image_size > 0 && <= OTA_IMAGE_MAX_PAYLOAD
         *   3. CRC-32/ISO-HDLC over [dl_addr+0x400, dl_addr+0x400+image_size) == header.crc32
         *
         * Returns 1 byte: 0x01 = PASS, 0x00 = FAIL.
         */
        if (g_flash_state.bytes_written < OTA_IMAGE_HDR_SIZE) {
            /* Not enough data written to hold a header — reject. */
            return -(int) 0x22; /* conditionsNotCorrect */
        }

        const ota_image_header_t *hdr =
            (const ota_image_header_t *) (uintptr_t) g_flash_state.dl_addr;

        /* The full payload must have been transferred before we CRC it,
         * otherwise validation would run over still-erased (0xFF) flash.
         * Check image_size first (overflow-safe) then the written count. */
        if (hdr->image_size > OTA_IMAGE_MAX_PAYLOAD ||
            g_flash_state.bytes_written <
                (uint32_t) OTA_IMAGE_HDR_SIZE + hdr->image_size) {
            return -(int) 0x22; /* conditionsNotCorrect: incomplete image */
        }

        /* Validate with the SAME routine the bootloader runs at boot, so a PASS
         * here guarantees the next-boot app_is_valid() also passes (magic +
         * size + CRC-32 + initial-SP-in-RAM) — no download/boot divergence. */
        uint8_t pass = app_is_valid(g_flash_state.dl_addr) ? 0x01u : 0x00u;

        if (max_len < 1u) {
            return -(int) 0x14;
        }
        out_buf[0] = pass;
        if (pass) {
            uart_puts("BL: image check PASS\n");
        } else {
            uart_puts("BL: image check FAIL\n");
        }
        return 1;
    }

    if (id == 0xFF02u) {
        /*
         * ActivateSoftware: mark the inactive bank as pending confirmation,
         * then swap banks and reset.  The pending flag ensures the bootloader
         * will roll back if the newly-activated app never calls boot_confirm().
         *
         * Order matters: boot_state_mark_pending() MUST complete before
         * flash_set_swap_and_reset() so the flag is visible on the next boot.
         * A power loss after mark_pending but before swap keeps the CURRENT
         * bank active; the inactive bank's pending flag is benign there
         * (it is never the active bank until a successful swap).
         */
        uint8_t  active    = flash_active_bank();
        uint32_t inactive_base = 0x08000000UL + (uint32_t)(!active) * 0x100000UL;
        uart_puts("BL: marking inactive bank pending\n");
        boot_state_mark_pending(inactive_base);
        uart_puts("BL: activate software\n");
        flash_set_swap_and_reset();
        /* flash_set_swap_and_reset() issues a system reset; this line is unreachable. */
        for (;;) {
        }
    }

    return -(int) 0x31; /* requestOutOfRange */
}

/* ---------------------------------------------------------------------------
 * DID table — 0xF1A0: active bank indicator (1 byte, read-only, all sessions)
 * ------------------------------------------------------------------------- */
static int bl_read_did(uds_ctx_t *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    if (did == 0xF1A0u && max_len >= 1u) {
        buf[0] = flash_active_bank();
        return 1;
    }
    return -(int) 0x31; /* requestOutOfRange */
}

static const uds_did_entry_t g_did_table[] = {
    {0xF1A0u, 1u, 0u, 0u, bl_read_did, NULL, NULL}
};

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
     * Boot decision with confirmation and automatic rollback.
     *
     * After ActivateSoftware swaps banks the newly-active bank is "pending":
     * it must confirm itself (by erasing its boot-state sector) within
     * MAX_BOOT_ATTEMPTS attempts.  A freshly-activated app that reboots
     * without confirming is rolled back to the previous (known-good) bank.
     *
     * State machine (boot_state_decide() in boot_state.h):
     *   JUMP         — bank confirmed or no record; validate image and jump.
     *   BUMP_AND_JUMP — bank on-trial; increment attempt counter, then jump.
     *   ROLLBACK     — attempt limit reached; clear pending flag, swap back.
     *
     * Safety properties:
     *   - Power-loss during download: inactive bank header check fails on
     *     next boot → BL-RECOVERY (existing behaviour, unchanged).
     *   - Power-loss after erase but before boot-state program: sector reads
     *     as all-0xFF → magic mismatch → pending=0 → JUMP (safe default).
     *   - Rolled-back-to bank must not be pending: ActivateSoftware only
     *     marks the INACTIVE bank before swapping, so the prior known-good
     *     bank never has its boot-state touched.
     */
    {
        uint8_t  active_bank     = flash_active_bank();
        uint32_t active_base     = 0x08000000UL + (uint32_t) active_bank * 0x100000UL;
        uint32_t active_app_base = active_base + 0x18000UL;

        boot_state_t bs;
        boot_state_read(active_base, &bs);

        boot_decision_t decision = boot_state_decide(&bs, MAX_BOOT_ATTEMPTS);

        if (decision == BOOT_DECISION_ROLLBACK) {
            uart_puts("BL-ROLLBACK\n");
            /* Clear the pending flag BEFORE swapping so the active bank is
             * clean; if we lose power here the bank stays current and the
             * next boot restarts the rollback decision (safe: rollback again). */
            boot_state_clear(active_base);
            /* Swap back to the other bank (the known-good one). */
            flash_set_swap_and_reset(); /* does not return */
            for (;;) {}
        }

        if (decision == BOOT_DECISION_BUMP_AND_JUMP) {
            /* Count this attempt before jumping so a crash/watchdog reset
             * is recorded even if the app never runs. */
            boot_state_bump_attempts(active_base);
        }

        /* Validate the image header + CRC before jumping. */
        if (app_is_valid(active_app_base)) {
            uart_puts("BL-JUMP\n");
            app_jump(active_app_base); /* does not return */
        }
        uart_puts("BL-RECOVERY\n");
    }

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

    /* DID table: 0xF1A0 active bank indicator. */
    cfg.did_table.entries = g_did_table;
    cfg.did_table.count   = 1u;

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

#ifdef SIM_OTA_TESTER
    sim_tester_init(DEMO_SECRET, DEMO_SEED);
#endif

    /* Polling loop — no NVIC IRQs. */
    for (;;) {
        /* Pump incoming CAN frames into the ISO-TP layer. */
        can_frame_t frame;
        while (fdcan_poll_rx_frame(&frame)) {
            if (frame.id == BL_RX_ID) {
                uds_isotp_rx_callback(&g_isotp, &g_uds, frame.id, frame.data, frame.len);
            }
#ifdef SIM_OTA_TESTER
            else if (frame.id == BL_TX_ID) {
                sim_tester_rx(frame.data, frame.len);
            }
#endif
        }

        /* Run the UDS service dispatcher. */
        uds_process(&g_uds);

        /* Advance ISO-TP timers and drive multi-frame TX. */
        uds_tp_isotp_process(&g_isotp, g_now_ms);

#ifdef SIM_OTA_TESTER
        sim_tester_poll();
#endif

        /* Increment the free-running millisecond counter. */
        ++g_now_ms;
    }
}
