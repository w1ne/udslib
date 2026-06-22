/**
 * flash_h5.c — STM32H563 Flash driver implementation
 *
 * Register map authoritative source: RM0481 §7 and stm32h563.svd.
 * See flash_h5.h for the full register/bit constant definitions.
 */

#include "flash_h5.h"

/* ---------------------------------------------------------------------------
 * flash_unlock
 *
 * Writes the two-step NSKEYR sequence to unlock the flash controller
 * for non-secure program and erase operations.
 * RM0481 §7.3.1 / stm32h563.svd NSKEYR @ offset 0x04.
 * ------------------------------------------------------------------------- */
void flash_unlock(void)
{
    FLASH_NSKEYR = FLASH_NSKEY1;
    FLASH_NSKEYR = FLASH_NSKEY2;
}

/* ---------------------------------------------------------------------------
 * flash_program
 *
 * Programs len bytes from data to the flash address addr in 16-byte
 * (quad-word) units as required by the H5 flash controller.
 *
 * Preconditions (caller-guaranteed):
 *   - addr 16-byte aligned
 *   - len  a non-zero multiple of 16
 *   - flash_unlock() already called
 *
 * RM0481 §7.3.4 / stm32h563.svd NSCR @ 0x28, NSSR @ 0x20.
 * ------------------------------------------------------------------------- */
int flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)addr;
    const uint32_t    *src = (const uint32_t *)(uintptr_t)data;

    /* Enable programming mode */
    FLASH_NSCR |= NSCR_PG;

    /*
     * Write in 16-byte (quad-word) groups.
     * i counts 32-bit words; each iteration advances by 4 words (16 bytes).
     */
    for (uint32_t i = 0u; i < len / 4u; i += 4u) {
        dst[i + 0u] = src[i + 0u];
        dst[i + 1u] = src[i + 1u];
        dst[i + 2u] = src[i + 2u];
        dst[i + 3u] = src[i + 3u];
        /* Wait for the quad-word write to complete */
        while (FLASH_NSSR & NSSR_BSY) {}
    }

    /* Clear programming enable */
    FLASH_NSCR &= ~NSCR_PG;

    return 0;
}

/* ---------------------------------------------------------------------------
 * flash_erase_sector
 *
 * Initiates a sector erase and waits for completion.
 *
 * NSCR layout written:
 *   SER | (bank ? BKSEL : 0) | (sector << SNB_SHIFT) | STRT
 *
 * RM0481 §7.3.5 / stm32h563.svd NSCR @ 0x28, NSSR @ 0x20.
 * ------------------------------------------------------------------------- */
int flash_erase_sector(uint8_t bank, uint32_t sector)
{
    uint32_t cr = NSCR_SER
                | ((uint32_t)(sector << NSCR_SNB_SHIFT) & NSCR_SNB_MASK)
                | NSCR_STRT;

    if (bank != 0u) {
        cr |= NSCR_BKSEL;
    }

    FLASH_NSCR = cr;

    while (FLASH_NSSR & NSSR_BSY) {}

    return 0;
}

/* ---------------------------------------------------------------------------
 * flash_set_swap_and_reset
 *
 * Programs OPTSR_PRG.SWAP_BANK and issues a system reset so the new bank
 * mapping takes effect on the next boot.
 *
 * NOTE: H5 has NO OBL_LAUNCH bit (unlike H7).  The swap is applied by the
 * hardware only after a system reset.  The LabWired sim resets the CPU
 * immediately upon OPTSTRT, so both paths are covered by this sequence.
 *
 * RM0481 §7.3.3 + §7.8.6/7.8.8 / stm32h563.svd OPTKEYR 0x0C, OPTSR_PRG
 * 0x54, OPTCR 0x1C.  SCB AIRCR per ARMv8-M ARM §B3.2.6.
 * ------------------------------------------------------------------------- */
void flash_set_swap_and_reset(void)
{
    /* Step 1: Unlock option bytes */
    FLASH_OPTKEYR = FLASH_OPTKEY1;
    FLASH_OPTKEYR = FLASH_OPTKEY2;

    /* Step 2: Set SWAP_BANK in the programming register */
    FLASH_OPTSR_PRG |= OPTSR_SWAP_BANK;

    /* Step 3: Commit — OPTSTRT programs the option byte */
    FLASH_OPTCR |= OPTCR_OPTSTRT;

    /* Step 4: Wait for the option byte write to complete */
    while (FLASH_NSSR & NSSR_BSY) {}

    /* Step 5: System reset — swap takes effect on next boot (real HW path).
     *         On the sim the CPU has already rebooted at OPTSTRT; the reset
     *         below is the hardware-correct path and is unreachable there. */
    SCB_AIRCR = AIRCR_RESET_KEY;

    /* Should not reach here */
    for (;;) {}
}

/* ---------------------------------------------------------------------------
 * flash_swap_to_bank_and_reset
 *
 * Sets OPTSR_PRG.SWAP_BANK to the value that selects target_bank, then issues
 * a system reset.  This is the explicit-target variant used by the 0xFF03
 * PerformRollback routine: calling flash_set_swap_and_reset() from bank 1
 * would be a no-op (bit already set), so we must write the exact desired value.
 *
 *   target_bank == 0 → clear SWAP_BANK  (bank 1 / 0x08000000 becomes active)
 *   target_bank == 1 → set   SWAP_BANK  (bank 2 / 0x08100000 becomes active)
 *
 * RM0481 §7.3.3 / stm32h563.svd OPTKEYR 0x0C, OPTSR_PRG 0x54, OPTCR 0x1C.
 * ------------------------------------------------------------------------- */
void flash_swap_to_bank_and_reset(uint8_t target_bank)
{
    /* Step 1: Unlock option bytes */
    FLASH_OPTKEYR = FLASH_OPTKEY1;
    FLASH_OPTKEYR = FLASH_OPTKEY2;

    /* Step 2: Write the desired SWAP_BANK value explicitly (set or clear) */
    if (target_bank != 0u) {
        FLASH_OPTSR_PRG |= OPTSR_SWAP_BANK;
    } else {
        FLASH_OPTSR_PRG &= ~OPTSR_SWAP_BANK;
    }

    /* Step 3: Commit the option byte write */
    FLASH_OPTCR |= OPTCR_OPTSTRT;

    /* Step 4: Wait for the option byte write to complete */
    while (FLASH_NSSR & NSSR_BSY) {}

    /* Step 5: System reset — swap takes effect on next boot. */
    SCB_AIRCR = AIRCR_RESET_KEY;

    /* Should not reach here */
    for (;;) {}
}

/* ---------------------------------------------------------------------------
 * flash_active_bank
 *
 * Reads OPTSR_CUR.SWAP_BANK (bit 31) to determine the currently active bank.
 * Returns 1 if bank 2 is active (SWAP_BANK set), 0 if bank 1 is active.
 *
 * RM0481 §7.8.8 / stm32h563.svd OPTSR_CUR @ 0x50.
 * ------------------------------------------------------------------------- */
uint8_t flash_active_bank(void)
{
    return ((FLASH_OPTSR_CUR & OPTSR_SWAP_BANK) != 0u) ? 1u : 0u;
}
