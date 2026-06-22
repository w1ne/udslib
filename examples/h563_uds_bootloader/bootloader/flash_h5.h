/**
 * flash_h5.h — STM32H563 Flash driver API
 *
 * Register access uses the ST CMSIS device header (stm32h563xx.h): the
 * FLASH_TypeDef instance (FLASH->NSKEYR/NSCR/NSSR/NSCCR/OPTKEYR/OPTCR/
 * OPTSR_PRG/OPTSR_CUR) and the header's bit macros (FLASH_CR_PG, FLASH_CR_SER,
 * FLASH_SR_BSY, ...).  No register addresses are hand-typed here.  H5 FLASH is
 * HAL-only in the Cube tree (no LL), so the CMSIS struct is the correct level.
 *
 * Register map authoritative source: RM0481 §7.
 *
 * NOTE: H5 programs flash in 16-byte (quad-word) units only.
 *       flash_program() callers must supply addr and len that are
 *       both multiples of 16 and addr must be 16-byte aligned.
 */

#ifndef FLASH_H5_H
#define FLASH_H5_H

#include <stdint.h>

/* The CMSIS device header (FLASH_TypeDef + FLASH_* bit macros) is needed only
 * by the ARM firmware build, which actually touches the registers.  The host
 * unit tests (boot_state_test) compile boot_state.c with the system gcc and
 * stub all flash I/O, using only this header's public API and RAMFUNC macro —
 * so the device header (and its ARM-only CMSIS core) must not be pulled in
 * there.  Guard it on __arm__, mirroring the RAMFUNC guard below. */
#if defined(__arm__)
#include "stm32h563xx.h"
#endif

/* ---------------------------------------------------------------------------
 * RAMFUNC — place a function in the .ramfunc section so it executes from SRAM.
 *
 * On STM32H5 silicon an erase/program of a flash bank cannot run while the CPU
 * fetches instructions from that same bank (read-while-write hazard, RM0481
 * §7.3.4).  Functions that issue an erase/program and then poll NSSR.BSY are
 * marked RAMFUNC; startup.c copies .ramfunc from its flash load address (LMA)
 * to RAM (VMA) before main(), and long_call lets a flash-resident caller reach
 * the RAM-resident routine regardless of the relative branch range.
 *
 * The attribute is enabled only for the ARM firmware build.  The host unit
 * test (boot_state_test) compiles boot_state.c with the system gcc, whose
 * default linker script has no .ramfunc section; long_call is also Arm-only.
 * Leaving the macro empty off-target keeps those host links clean.
 * ------------------------------------------------------------------------- */
#if defined(__arm__)
#define RAMFUNC  __attribute__((section(".ramfunc"), noinline, long_call))
#else
#define RAMFUNC
#endif

/* ---------------------------------------------------------------------------
 * Unlock keys  (RM0481 §7.3.1)
 * NSKEYR keys — unlock non-secure program/erase operations
 * OPTKEYR keys — unlock option byte writes
 * These are architectural magic values, not register addresses.
 * ------------------------------------------------------------------------- */
#define FLASH_NSKEY1        0x45670123UL
#define FLASH_NSKEY2        0xCDEF89ABUL

#define FLASH_OPTKEY1       0x08192A3BUL
#define FLASH_OPTKEY2       0x4C5D6E7FUL

/* ---------------------------------------------------------------------------
 * Aggregate of all program/erase error flags in NSSR (RM0481 §7.8.7).
 *
 * The error/EOP flags are sticky and read-only in NSSR; they are cleared by
 * writing a 1 to the corresponding bit in NSCCR (NOT by writing NSSR).  The
 * individual bit macros come from the CMSIS device header (FLASH_SR_*).
 * ------------------------------------------------------------------------- */
#define NSSR_ERR_MASK \
    (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR)

/* ---------------------------------------------------------------------------
 * Bounded-wait iteration cap.
 *
 * A program or erase that does not clear BSY within this many polling
 * iterations is treated as a stuck controller and reported as a timeout
 * (FLASH_ERR_TIMEOUT).  The value is large enough never to trip on a healthy
 * op (an 8 KB sector erase is a few ms) yet finite so a faulted controller can
 * no longer hang the CPU.
 * ------------------------------------------------------------------------- */
#define FLASH_BSY_TIMEOUT   0x10000000UL

/* ---------------------------------------------------------------------------
 * Driver error codes (returned by flash_program / flash_erase_sector).
 * Any non-zero value propagates as a programming failure to the UDS layer.
 * ------------------------------------------------------------------------- */
#define FLASH_OK            0
#define FLASH_ERR_TIMEOUT   1            /* BSY never cleared within the cap */
#define FLASH_ERR_HW        2            /* NSSR reported a program/erase error */

/* ---------------------------------------------------------------------------
 * System reset key for SCB AIRCR  (ARMv8-M ARM §B3.2.6).
 * VECTKEY=0x05FA | SYSRESETREQ.  Kept as a named constant so the reset write
 * matches the architectural value exactly.
 * ------------------------------------------------------------------------- */
#define AIRCR_RESET_KEY     (0x05FA0004UL)

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * flash_unlock() — Unlock the flash controller for program/erase.
 *
 * Writes the two NSKEYR magic values.  Must be called before any
 * flash_program() or flash_erase_sector() operation.
 */
void flash_unlock(void);

/**
 * flash_program() — Write len bytes from data to flash address addr.
 *
 * H5 flash requires 16-byte (quad-word) aligned writes.
 * Caller MUST guarantee:
 *   - addr is 16-byte aligned
 *   - len is a non-zero multiple of 16
 *   - data points to at least len bytes
 *   - flash_unlock() has already been called
 *
 * Returns 0 on success.
 */
RAMFUNC int flash_program(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * flash_erase_sector() — Erase one flash sector.
 *
 * @param bank   0 = bank 1, non-zero = bank 2
 * @param sector Sector number within the selected bank (0-based)
 *
 * Caller must have called flash_unlock() first.
 * Returns 0 on success.
 */
RAMFUNC int flash_erase_sector(uint8_t bank, uint32_t sector);

/**
 * flash_set_swap_and_reset() — Program the SWAP_BANK option bit and reset.
 *
 * Sequence (RM0481 §7.3.3 — no OBL_LAUNCH on H5):
 *   1. Unlock option bytes via OPTKEYR.
 *   2. Set OPTSR_PRG.SWAP_BANK.
 *   3. Set OPTCR.OPTSTART to commit the option byte write.
 *   4. Wait for BSY to clear.
 *   5. Issue a system reset via SCB AIRCR so the new bank mapping takes effect.
 *
 * On the LabWired H563 sim the OPTSTART write triggers swap+CPU-reset
 * immediately; on real silicon the reset in step 5 is what applies the swap.
 * Both paths are correct with this full sequence.
 *
 * NOTE: This function always SETS SWAP_BANK (activates bank 1 from bank 0).
 * Use flash_swap_to_bank_and_reset() when the target bank must be specified
 * explicitly (e.g. tester-commanded rollback from bank 1 back to bank 0).
 *
 * Does not return.
 */
void flash_set_swap_and_reset(void);

/**
 * flash_swap_to_bank_and_reset() — Set OPTSR_PRG.SWAP_BANK to select
 * @p target_bank and issue a system reset.
 *
 * Unlike flash_set_swap_and_reset() which always sets the bit (only safe when
 * the current bank is 0), this function explicitly writes the bit value that
 * selects @p target_bank:
 *   target_bank == 0 → SWAP_BANK cleared (bank 1 mapped at 0x08000000)
 *   target_bank == 1 → SWAP_BANK set    (bank 2 mapped at 0x08000000)
 *
 * Used by 0xFF03 PerformRollback to revert to the other bank regardless of
 * which bank is currently active.
 *
 * @param target_bank  0 or 1.
 * Does not return.
 */
void flash_swap_to_bank_and_reset(uint8_t target_bank);

/**
 * flash_active_bank() — Return the currently active boot bank.
 *
 * Reads OPTSR_CUR.SWAP_BANK.
 * Returns 1 if bank 2 is active (SWAP_BANK set), 0 if bank 1 is active.
 */
uint8_t flash_active_bank(void);

#endif /* FLASH_H5_H */
