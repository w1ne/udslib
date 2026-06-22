/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_confirm.c
 * @brief App-side boot-confirmation helper.
 *
 * Erases the active bank's boot-state sector (sector 11, bank-relative
 * offset 0x16000).  Under SWAP_BANK the active bank always maps to the
 * physical range starting at 0x08000000, so the boot-state sector is
 * always at 0x08016000 from the app's point of view.
 *
 * Register constants are the same as in the bootloader flash_h5.c/h.
 * The app is freestanding (no libc), so we access MMIO directly.
 *
 * See boot_confirm.h for limitations (RAM-execute requirement on real HW,
 * non-atomicity across power loss).
 */

#include "boot_confirm.h"
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * MMIO helpers (same constants as bootloader flash_h5.h)
 * ------------------------------------------------------------------------- */
#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))

#define FLASH_BASE      0x40022000UL
#define FLASH_NSKEYR    REG32(FLASH_BASE + 0x04U)
#define FLASH_NSSR      REG32(FLASH_BASE + 0x20U)
#define FLASH_NSCR      REG32(FLASH_BASE + 0x28U)

#define FLASH_NSKEY1    0x45670123UL
#define FLASH_NSKEY2    0xCDEF89ABUL

#define NSSR_BSY        (1UL << 0)
/* Finite cap on the BSY spin so a stuck flash controller cannot hang the app
 * (mirrors FLASH_BSY_TIMEOUT in the bootloader's flash_h5.h). */
#define NSSR_BSY_TIMEOUT 0x10000000UL
#define NSCR_SER        (1UL << 2)
#define NSCR_STRT       (1UL << 5)
#define NSCR_SNB_SHIFT  6U
#define NSCR_SNB_MASK   (0x7FUL << NSCR_SNB_SHIFT)
/* NSCR_BKSEL (bit 31) selects bank 2; omitted here because the active bank
 * is always mapped to bank 1's physical address space under SWAP_BANK. */

/*
 * Boot-state sector number within the active bank (sector 11, 0-based).
 * Erase at 0x08016000 (bank_base + 11 * 8 KB = bank_base + 0x16000).
 */
#define BOOT_STATE_SECTOR  11u

/* ---------------------------------------------------------------------------
 * boot_confirm
 * ------------------------------------------------------------------------- */
void boot_confirm(void)
{
    /* Unlock the flash controller. The app is a standalone payload that does
     * NOT link the bootloader's flash_h5 driver, so it re-issues the NSKEYR
     * sequence directly here (identical to flash_h5's flash_unlock()). */
    FLASH_NSKEYR = FLASH_NSKEY1;
    FLASH_NSKEYR = FLASH_NSKEY2;

    /*
     * Erase sector 11 of the active bank (BKSEL=0 — the active bank always
     * appears as bank 1 in the flash controller's view after SWAP_BANK takes
     * effect).
     */
    uint32_t cr = NSCR_SER
                | ((uint32_t)(BOOT_STATE_SECTOR << NSCR_SNB_SHIFT) & NSCR_SNB_MASK)
                | NSCR_STRT;
    FLASH_NSCR = cr;

    /* Wait for the erase to complete (bounded — never spin forever on a
     * stuck controller). boot_confirm() returns void; a timeout simply stops
     * waiting. */
    uint32_t guard = NSSR_BSY_TIMEOUT;
    while ((FLASH_NSSR & NSSR_BSY) != 0u) {
        if (--guard == 0u) {
            break;
        }
    }
}
