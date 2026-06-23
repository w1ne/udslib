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
 * Register access uses the ST CMSIS device header (stm32h563xx.h): the
 * FLASH_TypeDef instance and the header's FLASH_CR_/FLASH_SR_/FLASH_OPTSR_ bit
 * macros — the same level the bootloader's flash_h5.c uses.  No register
 * addresses are hand-typed.  The app is freestanding (no libc); the CMSIS
 * device header compiles cleanly under -ffreestanding.
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

#include "stm32h563xx.h"

/* The two NSKEYR unlock magic values (RM0481 §7.3.1) — architectural
 * constants, not register addresses. */
#define FLASH_NSKEY1 0x45670123UL
#define FLASH_NSKEY2 0xCDEF89ABUL

/* Finite cap on the BSY spin so a stuck flash controller cannot hang the app
 * (mirrors FLASH_BSY_TIMEOUT in the bootloader's flash_h5.h). */
#define NSSR_BSY_TIMEOUT 0x10000000UL

/*
 * Boot-state sector number within the active bank (sector 11, 0-based).
 * Erase at 0x08016000 (bank_base + 11 * 8 KB = bank_base + 0x16000).
 */
#define BOOT_STATE_SECTOR 11u

/* ---------------------------------------------------------------------------
 * boot_confirm
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_confirm(void)
{
    /* Unlock the flash controller. The app is a standalone payload that does
     * NOT link the bootloader's flash_h5 driver, so it re-issues the NSKEYR
     * sequence directly here (identical to flash_h5's flash_unlock()). */
    FLASH->NSKEYR = FLASH_NSKEY1;
    FLASH->NSKEYR = FLASH_NSKEY2;

    /*
     * Erase sector 11 of the bank we are actually running from.  The active
     * (running) physical bank is bank 2 iff OPTSR_CUR.SWAP_BANK is set; select
     * it via NSCR.BKSEL.  This matches the bootloader's flash_erase_sector(),
     * which likewise selects the erase target by physical bank index.
     */
    uint32_t cr = FLASH_CR_SER |
                  (((uint32_t) BOOT_STATE_SECTOR << FLASH_CR_SNB_Pos) & FLASH_CR_SNB_Msk) |
                  FLASH_CR_START;
    if ((FLASH->OPTSR_CUR & FLASH_OPTSR_SWAP_BANK) != 0u) {
        cr |= FLASH_CR_BKSEL; /* bank 2 is the running bank */
    }
    FLASH->NSCR = cr;

    /* Wait for the erase to complete (bounded — never spin forever on a
     * stuck controller). boot_confirm() returns void; a timeout simply stops
     * waiting. */
    uint32_t guard = NSSR_BSY_TIMEOUT;
    while ((FLASH->NSSR & FLASH_SR_BSY) != 0u) {
        if (--guard == 0u) {
            break;
        }
    }
}
