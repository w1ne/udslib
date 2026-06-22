/**
 * flash_h5.h — STM32H563 Flash driver register map and API
 *
 * All register offsets and bit definitions are taken from:
 *   RM0481 §7 (Flash memory interface) and stm32h563.svd.
 *
 * Flash controller base address: 0x40022000
 *
 * NOTE: H5 programs flash in 16-byte (quad-word) units only.
 *       flash_program() callers must supply addr and len that are
 *       both multiples of 16 and addr must be 16-byte aligned.
 */

#ifndef FLASH_H5_H
#define FLASH_H5_H

#include <stdint.h>

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
#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))
#else
#define RAMFUNC
#endif

/* ---------------------------------------------------------------------------
 * MMIO helper
 * ------------------------------------------------------------------------- */
#define REG32(a) (*(volatile uint32_t *) (uintptr_t) (a))

/* ---------------------------------------------------------------------------
 * Flash controller base  (RM0481 §7 / stm32h563.svd FLASH peripheral)
 * ------------------------------------------------------------------------- */
#define FLASH_BASE 0x40022000UL

/* ---------------------------------------------------------------------------
 * Register addresses
 * RM0481 Table 7-2 / stm32h563.svd:
 *   NSKEYR   @ 0x04  — non-secure flash key register  (unlock program/erase)
 *   SECKEYR  @ 0x08  — secure flash key register      (NOT used here)
 *   OPTKEYR  @ 0x0C  — option-byte key register       (unlock option bytes)
 *   OPTCR    @ 0x1C  — option control register
 *   NSSR     @ 0x20  — non-secure status register
 *   NSCR     @ 0x28  — non-secure control register
 *   NSCCR    @ 0x30  — non-secure clear control register (write-1-clears NSSR flags)
 *   OPTSR_CUR@ 0x50  — option status register (current)
 *   OPTSR_PRG@ 0x54  — option status register (to program)
 * ------------------------------------------------------------------------- */
#define FLASH_NSKEYR REG32(FLASH_BASE + 0x04U)
#define FLASH_OPTKEYR REG32(FLASH_BASE + 0x0CU)
#define FLASH_OPTCR REG32(FLASH_BASE + 0x1CU)
#define FLASH_NSSR REG32(FLASH_BASE + 0x20U)
#define FLASH_NSCR REG32(FLASH_BASE + 0x28U)
#define FLASH_NSCCR REG32(FLASH_BASE + 0x30U)
#define FLASH_OPTSR_CUR REG32(FLASH_BASE + 0x50U)
#define FLASH_OPTSR_PRG REG32(FLASH_BASE + 0x54U)

/* ---------------------------------------------------------------------------
 * Unlock keys  (RM0481 §7.3.1)
 * NSKEYR keys — unlock non-secure program/erase operations
 * OPTKEYR keys — unlock option byte writes
 * ------------------------------------------------------------------------- */
#define FLASH_NSKEY1 0x45670123UL
#define FLASH_NSKEY2 0xCDEF89ABUL

#define FLASH_OPTKEY1 0x08192A3BUL
#define FLASH_OPTKEY2 0x4C5D6E7FUL

/* ---------------------------------------------------------------------------
 * FLASH_NSSR bits  (RM0481 §7.8.7 / stm32h563.svd NSSR)
 *
 * The error/EOP flags are sticky and read-only in NSSR; they are cleared by
 * writing a 1 to the corresponding bit in NSCCR (NOT by writing NSSR).
 * ------------------------------------------------------------------------- */
#define NSSR_BSY (1UL << 0)      /* Non-secure operation busy */
#define NSSR_WBNE (1UL << 1)     /* Write buffer not empty */
#define NSSR_EOP (1UL << 16)     /* End of operation */
#define NSSR_WRPERR (1UL << 17)  /* Write protection error */
#define NSSR_PGSERR (1UL << 18)  /* Programming sequence error */
#define NSSR_STRBERR (1UL << 19) /* Strobe (alignment) error */
#define NSSR_INCERR (1UL << 20)  /* Inconsistency error */

/* Aggregate of all program/erase error flags. */
#define NSSR_ERR_MASK (NSSR_WRPERR | NSSR_PGSERR | NSSR_STRBERR | NSSR_INCERR)

/* ---------------------------------------------------------------------------
 * Bounded-wait iteration cap.
 *
 * Replaces the previous unbounded `while (NSSR & BSY)` spins.  A program or
 * erase that does not clear BSY within this many polling iterations is treated
 * as a stuck controller and reported as a timeout (FLASH_ERR_TIMEOUT).  The
 * value is large enough never to trip on a healthy op (an 8 KB sector erase is
 * a few ms) yet finite so a faulted controller can no longer hang the CPU.
 * ------------------------------------------------------------------------- */
#define FLASH_BSY_TIMEOUT 0x10000000UL

/* ---------------------------------------------------------------------------
 * Driver error codes (returned by flash_program / flash_erase_sector).
 * Any non-zero value propagates as a programming failure to the UDS layer.
 * ------------------------------------------------------------------------- */
#define FLASH_OK 0
#define FLASH_ERR_TIMEOUT 1 /* BSY never cleared within the cap */
#define FLASH_ERR_HW 2      /* NSSR reported a program/erase error */

/* ---------------------------------------------------------------------------
 * FLASH_NSCR bits  (RM0481 §7.8.9 / stm32h563.svd NSCR)
 * ------------------------------------------------------------------------- */
#define NSCR_LOCK (1UL << 0)                     /* Flash lock */
#define NSCR_PG (1UL << 1)                       /* Programming enable */
#define NSCR_SER (1UL << 2)                      /* Sector erase */
#define NSCR_BER (1UL << 3)                      /* Bank erase */
#define NSCR_STRT (1UL << 5)                     /* Start erase operation */
#define NSCR_SNB_SHIFT 6U                        /* Sector number field LSB */
#define NSCR_SNB_MASK (0x7FUL << NSCR_SNB_SHIFT) /* bits[12:6] */
#define NSCR_BKSEL (1UL << 31)                   /* Bank select (0=bank1, 1=bank2) */

/* ---------------------------------------------------------------------------
 * FLASH_OPTCR bits  (RM0481 §7.8.6 / stm32h563.svd OPTCR)
 * NOTE: H5 has NO OBL_LAUNCH bit; swap takes effect on the next reset.
 * ------------------------------------------------------------------------- */
#define OPTCR_OPTLOCK (1UL << 0) /* Option bytes lock */
#define OPTCR_OPTSTRT (1UL << 1) /* Option byte start programming */

/* ---------------------------------------------------------------------------
 * FLASH_OPTSR_CUR / FLASH_OPTSR_PRG bits  (RM0481 §7.8.8 / stm32h563.svd)
 * ------------------------------------------------------------------------- */
#define OPTSR_SWAP_BANK (1UL << 31) /* Bank swap active (CUR) / request (PRG) */

/* ---------------------------------------------------------------------------
 * NVIC / SCB — System reset via AIRCR
 * ARMv8-M Architecture Reference Manual §B3.2.6
 * ------------------------------------------------------------------------- */
#define SCB_AIRCR REG32(0xE000ED0CUL)
#define AIRCR_RESET_KEY (0x05FA0004UL) /* VECTKEY=0x05FA | SYSRESETREQ */

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
 * Sequence (RM0481 §7.3.3 / SVD-confirmed — no OBL_LAUNCH on H5):
 *   1. Unlock option bytes via OPTKEYR.
 *   2. Set OPTSR_PRG.SWAP_BANK.
 *   3. Set OPTCR.OPTSTRT to commit the option byte write.
 *   4. Wait for BSY to clear.
 *   5. Issue a system reset via SCB AIRCR so the new bank mapping takes effect.
 *
 * On the LabWired H563 sim the OPTSTRT write triggers swap+CPU-reset
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
