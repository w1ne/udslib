/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_confirm.c
 * @brief App-side boot-confirmation helper.
 *
 * Erases the active bank's boot-state sector (sector 11, bank-relative
 * offset 0x16000).  Under SWAP_BANK the active bank always READS at the
 * physical range starting at 0x08000000, so the boot-state sector reads at
 * 0x08016000 from the app's point of view.  The flash ERASE target, however,
 * is selected by NSCR.BKSEL by PHYSICAL bank (not by the swapped read view) —
 * exactly as the bootloader's flash_erase_sector() does.  So this code reads
 * OPTSR_CUR.SWAP_BANK to learn the running physical bank and sets BKSEL to
 * match, erasing the boot-state sector of the bank it is actually running
 * from.  (A hardcoded BKSEL=0 would erase bank 1 even when bank 2 is the
 * active/running bank, i.e. after the first OTA swap — never clearing the
 * pending flag and looping into a spurious rollback.)
 *
 * Register constants are the same as in the bootloader flash_h5.c/h.
 * The app is freestanding (no libc), so we access MMIO directly.
 *
 * boot_confirm() is marked RAMFUNC so it executes from SRAM (the active bank
 * cannot be read while it is being erased — RM0481 §7.3.4 read-while-write).
 * startup.c copies the .ramfunc section to RAM before main() calls this.
 *
 * See boot_confirm.h for the remaining limitation (non-atomicity across
 * power loss).
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
#define FLASH_OPTSR_CUR REG32(FLASH_BASE + 0x50U)

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
/* NSCR_BKSEL (bit 31) selects the PHYSICAL bank to erase (0=bank1, 1=bank2).
 * The erase target is by physical bank, NOT by the SWAP_BANK-remapped read
 * view — so we set it from OPTSR_CUR.SWAP_BANK (the running physical bank). */
#define NSCR_BKSEL      (1UL << 31)
/* OPTSR_CUR.SWAP_BANK (bit 31): 1 when bank 2 is the active/running bank. */
#define OPTSR_SWAP_BANK (1UL << 31)

/*
 * Boot-state sector number within the active bank (sector 11, 0-based).
 * Erase at 0x08016000 (bank_base + 11 * 8 KB = bank_base + 0x16000).
 */
#define BOOT_STATE_SECTOR  11u

/* ---------------------------------------------------------------------------
 * boot_confirm
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_confirm(void)
{
    /* Unlock the flash controller. The app is a standalone payload that does
     * NOT link the bootloader's flash_h5 driver, so it re-issues the NSKEYR
     * sequence directly here (identical to flash_h5's flash_unlock()). */
    FLASH_NSKEYR = FLASH_NSKEY1;
    FLASH_NSKEYR = FLASH_NSKEY2;

    /*
     * Erase sector 11 of the bank we are actually running from.  The active
     * (running) physical bank is bank 2 iff OPTSR_CUR.SWAP_BANK is set; select
     * it via NSCR.BKSEL.  This matches the bootloader's flash_erase_sector(),
     * which likewise selects the erase target by physical bank index.
     */
    uint32_t cr = NSCR_SER
                | ((uint32_t)(BOOT_STATE_SECTOR << NSCR_SNB_SHIFT) & NSCR_SNB_MASK)
                | NSCR_STRT;
    if ((FLASH_OPTSR_CUR & OPTSR_SWAP_BANK) != 0u) {
        cr |= NSCR_BKSEL; /* bank 2 is the running bank */
    }
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
