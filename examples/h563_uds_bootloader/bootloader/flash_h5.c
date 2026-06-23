/**
 * flash_h5.c — STM32H563 Flash driver implementation
 *
 * Register access via the ST CMSIS device header (stm32h563xx.h): the
 * FLASH_TypeDef instance and the header's FLASH_CR_/FLASH_SR_/FLASH_CCR_/
 * FLASH_OPTCR_/FLASH_OPTSR_ bit macros.  No register addresses are hand-typed.
 * H5 FLASH is HAL-only in the Cube tree (no LL driver), so the CMSIS struct is
 * the correct abstraction level.
 *
 * Register map authoritative source: RM0481 §7.
 * See flash_h5.h for the API and the unlock-key/error-code constants.
 */

#include "flash_h5.h"

/* ---------------------------------------------------------------------------
 * flash_wait_bsy  (internal)
 *
 * Spin on NSSR.BSY with a finite iteration cap so a stuck flash controller can
 * no longer hang the CPU.  Returns FLASH_OK when BSY clears within the budget,
 * FLASH_ERR_TIMEOUT otherwise.
 * ------------------------------------------------------------------------- */
RAMFUNC static int flash_wait_bsy(void)
{
    uint32_t guard = FLASH_BSY_TIMEOUT;
    while ((FLASH->NSSR & FLASH_SR_BSY) != 0u) {
        if (--guard == 0u) {
            return FLASH_ERR_TIMEOUT;
        }
    }
    return FLASH_OK;
}

/* ---------------------------------------------------------------------------
 * flash_check_and_clear  (internal)
 *
 * Inspect NSSR after an operation.  If any error flag is set, clear it via
 * NSCCR (write-1-clears) and report FLASH_ERR_HW.  On a clean operation clear
 * the sticky EOP flag (also via NSCCR) and report FLASH_OK.
 *
 * The NSSR error/EOP bit positions match the NSCCR clear-bit positions
 * (RM0481 §7.8.7 / §7.8.10), so a mask built from FLASH_SR_* clears the same
 * bits when written to NSCCR.
 * ------------------------------------------------------------------------- */
RAMFUNC static int flash_check_and_clear(void)
{
    uint32_t sr = FLASH->NSSR;
    if ((sr & NSSR_ERR_MASK) != 0u) {
        FLASH->NSCCR = sr & NSSR_ERR_MASK;
        return FLASH_ERR_HW;
    }
    FLASH->NSCCR = FLASH_SR_EOP;
    return FLASH_OK;
}

/* ---------------------------------------------------------------------------
 * flash_unlock
 *
 * Writes the two-step NSKEYR sequence to unlock the flash controller
 * for non-secure program and erase operations.  RM0481 §7.3.1.
 * ------------------------------------------------------------------------- */
void flash_unlock(void)
{
    FLASH->NSKEYR = FLASH_NSKEY1;
    FLASH->NSKEYR = FLASH_NSKEY2;
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
 * RM0481 §7.3.4.
 * ------------------------------------------------------------------------- */
RAMFUNC int flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    volatile uint32_t *dst = (volatile uint32_t *) (uintptr_t) addr;
    const uint32_t *src = (const uint32_t *) (uintptr_t) data;

    /* Enable programming mode */
    FLASH->NSCR |= FLASH_CR_PG;

    /*
     * Write in 16-byte (quad-word) groups.
     * i counts 32-bit words; each iteration advances by 4 words (16 bytes).
     */
    for (uint32_t i = 0u; i < len / 4u; i += 4u) {
        dst[i + 0u] = src[i + 0u];
        dst[i + 1u] = src[i + 1u];
        dst[i + 2u] = src[i + 2u];
        dst[i + 3u] = src[i + 3u];
        /* Wait for the quad-word write to complete (bounded). */
        if (flash_wait_bsy() != FLASH_OK) {
            FLASH->NSCR &= ~FLASH_CR_PG;
            return FLASH_ERR_TIMEOUT;
        }
        /* Check for a hardware program error (e.g. INCERR on a misaligned
         * quad-word, WRPERR on a protected sector).  On error the controller
         * has committed nothing; clear the flag and abort. */
        if (flash_check_and_clear() != FLASH_OK) {
            FLASH->NSCR &= ~FLASH_CR_PG;
            return FLASH_ERR_HW;
        }
    }

    /* Clear programming enable */
    FLASH->NSCR &= ~FLASH_CR_PG;

    return FLASH_OK;
}

/* ---------------------------------------------------------------------------
 * flash_erase_sector
 *
 * Initiates a sector erase and waits for completion.
 *
 * NSCR layout written:
 *   SER | (bank ? BKSEL : 0) | (sector << SNB_Pos) | START
 *
 * RM0481 §7.3.5.
 * ------------------------------------------------------------------------- */
RAMFUNC int flash_erase_sector(uint8_t bank, uint32_t sector)
{
    uint32_t cr = FLASH_CR_SER | ((sector << FLASH_CR_SNB_Pos) & FLASH_CR_SNB_Msk) | FLASH_CR_START;

    if (bank != 0u) {
        cr |= FLASH_CR_BKSEL;
    }

    FLASH->NSCR = cr;

    if (flash_wait_bsy() != FLASH_OK) {
        return FLASH_ERR_TIMEOUT;
    }
    if (flash_check_and_clear() != FLASH_OK) {
        return FLASH_ERR_HW;
    }

    return FLASH_OK;
}

/* ---------------------------------------------------------------------------
 * flash_set_swap_and_reset
 *
 * Programs OPTSR_PRG.SWAP_BANK and issues a system reset so the new bank
 * mapping takes effect on the next boot.
 *
 * NOTE: H5 has NO OBL_LAUNCH bit (unlike H7).  The swap is applied by the
 * hardware only after a system reset.  The LabWired sim resets the CPU
 * immediately upon OPTSTART, so both paths are covered by this sequence.
 *
 * RM0481 §7.3.3 + §7.8.6/7.8.8.  SCB AIRCR per ARMv8-M ARM §B3.2.6.
 * ------------------------------------------------------------------------- */
void flash_set_swap_and_reset(void)
{
    /* Step 1: Unlock option bytes */
    FLASH->OPTKEYR = FLASH_OPTKEY1;
    FLASH->OPTKEYR = FLASH_OPTKEY2;

    /* Step 2: Set SWAP_BANK in the programming register */
    FLASH->OPTSR_PRG |= FLASH_OPTSR_SWAP_BANK;

    /* Step 3: Commit — OPTSTART programs the option byte */
    FLASH->OPTCR |= FLASH_OPTCR_OPTSTART;

    /* Step 4: Wait for the option byte write to complete (bounded).
     * A timeout here intentionally falls through to the system reset rather
     * than hanging: the reset is what applies the swap on real silicon, and
     * stalling forever is never an acceptable outcome on this path. */
    (void) flash_wait_bsy();

    /* Step 5: System reset — swap takes effect on next boot (real HW path).
     *         On the sim the CPU has already rebooted at OPTSTART; the reset
     *         below is the hardware-correct path and is unreachable there. */
    SCB->AIRCR = AIRCR_RESET_KEY;

    /* Should not reach here */
    for (;;) {
    }
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
 * RM0481 §7.3.3.
 * ------------------------------------------------------------------------- */
void flash_swap_to_bank_and_reset(uint8_t target_bank)
{
    /* Step 1: Unlock option bytes */
    FLASH->OPTKEYR = FLASH_OPTKEY1;
    FLASH->OPTKEYR = FLASH_OPTKEY2;

    /* Step 2: Write the desired SWAP_BANK value explicitly (set or clear) */
    if (target_bank != 0u) {
        FLASH->OPTSR_PRG |= FLASH_OPTSR_SWAP_BANK;
    }
    else {
        FLASH->OPTSR_PRG &= ~FLASH_OPTSR_SWAP_BANK;
    }

    /* Step 3: Commit the option byte write */
    FLASH->OPTCR |= FLASH_OPTCR_OPTSTART;

    /* Step 4: Wait for the option byte write to complete (bounded).
     * A timeout here intentionally falls through to the system reset rather
     * than hanging: the reset is what applies the swap on real silicon, and
     * stalling forever is never an acceptable outcome on this path. */
    (void) flash_wait_bsy();

    /* Step 5: System reset — swap takes effect on next boot. */
    SCB->AIRCR = AIRCR_RESET_KEY;

    /* Should not reach here */
    for (;;) {
    }
}

/* ---------------------------------------------------------------------------
 * flash_active_bank
 *
 * Reads OPTSR_CUR.SWAP_BANK (bit 31) to determine the currently active bank.
 * Returns 1 if bank 2 is active (SWAP_BANK set), 0 if bank 1 is active.
 *
 * RM0481 §7.8.8.
 * ------------------------------------------------------------------------- */
uint8_t flash_active_bank(void)
{
    return ((FLASH->OPTSR_CUR & FLASH_OPTSR_SWAP_BANK) != 0u) ? 1u : 0u;
}
