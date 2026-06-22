/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state.h
 * @brief Per-bank boot-confirmation flag and rollback counter.
 *
 * Flash layout (dual-bank, 8 KB sectors, 96 KB BL region per bank):
 *
 *   Sector 11 of each bank (bank-relative offset 0x16000, 8 KB):
 *     BOOT_STATE_OFFSET = 0x16000   (sector 11 × 8 KB)
 *
 *   This sector is the LAST sector of the 96 KB bootloader region
 *   (sectors 0–11, 0x00000–0x17FFF).  BL code currently tops out at
 *   ~27 KB (well below 0x16000 = 90 KB), leaving this sector free.
 *
 *   The sector swaps WITH its bank under hardware SWAP_BANK, so the
 *   active-bank state is always accessible at the canonical address
 *   BOOT_STATE_BANK1_ADDR (0x08016000).
 *
 * Structure:
 *   typedef struct {
 *       uint32_t magic;     -- BOOT_STATE_MAGIC when record is valid
 *       uint32_t pending;   -- 1 = this bank is on-trial (unconfirmed)
 *       uint32_t attempts;  -- number of boot attempts so far
 *       uint32_t reserved;  -- 0xFF…FF (written as all-ones on erase)
 *   } boot_state_t;
 *
 * Wear note:
 *   Each boot_state_write() call erases the entire 8 KB sector then
 *   programs the 16-byte struct.  In a production design this sector
 *   should be used as a log/ring-buffer to spread wear across the
 *   sector.  For this simulation example the single-record scheme is
 *   sufficient.
 *
 * Atomicity note:
 *   Erase + program is NOT atomic across power loss.  If the MCU loses
 *   power after the erase but before the program completes, the sector
 *   will read as all-0xFF, which does not match BOOT_STATE_MAGIC;
 *   boot_state_read() will return pending=0, which is the safe (no
 *   rollback) default.  The pending flag is written to the INACTIVE
 *   bank BEFORE flash_set_swap_and_reset(), so a power loss during
 *   the write keeps the current bank active.
 *
 * Same-bank erase note (simulator):
 *   The bootloader erases and programs a sector of the bank it is
 *   executing from.  On real H5 silicon this is safe ONLY when the
 *   flash operation is driven from RAM (RM0481 §7.3.4 "Read-while-
 *   write" constraint).  In the LabWired H563 simulator the constraint
 *   does not apply; the simulation always completes correctly.
 *   Production firmware must copy the flash driver to ITCM/SRAM before
 *   calling boot_state_write().
 */

#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** Magic value that marks a valid boot_state_t record. */
#define BOOT_STATE_MAGIC  0xB007A5A5u

/**
 * Bank-relative byte offset of the boot-state sector (sector 11 × 8 KB).
 * The sector covers [bank_base + 0x16000 .. bank_base + 0x18000).
 */
#define BOOT_STATE_OFFSET  0x16000UL

/**
 * Canonical flash address of the active-bank boot-state sector when no
 * SWAP_BANK is active (bank 1 view).  Used in bootloader.ld as a linker
 * symbol.  Under SWAP_BANK the hardware re-maps so the active bank
 * always appears at 0x08000000; the offset is the same either way.
 */
#define BOOT_STATE_BANK1_ADDR  0x08016000UL

/** Sector index of the boot-state sector within its bank (0-based). */
#define BOOT_STATE_SECTOR  11u

/**
 * Maximum boot attempts before an unconfirmed app triggers a rollback.
 * The rolled-back-to bank must NOT be pending, so the next boot after
 * a rollback lands on the known-good bank and proceeds straight to BL-JUMP.
 */
#define MAX_BOOT_ATTEMPTS  3u

/* ---------------------------------------------------------------------------
 * Data structure
 * ------------------------------------------------------------------------- */

/**
 * Boot-state record stored at the start of the boot-state sector.
 *
 * Size: 16 bytes (one quad-word, matching H5 flash program granularity).
 */
typedef struct {
    uint32_t magic;    /**< BOOT_STATE_MAGIC when valid.           */
    uint32_t pending;  /**< 1 = unconfirmed (on-trial); 0 = good.  */
    uint32_t attempts; /**< Number of boot attempts so far.         */
    uint32_t reserved; /**< Unused; all-ones after sector erase.    */
} boot_state_t;

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * boot_state_read() — Read the boot-state record for a bank.
 *
 * @param bank_base  Physical base address of the bank (0x08000000 or
 *                   0x08100000).
 * @param out        Output buffer.  Filled from flash at
 *                   bank_base + BOOT_STATE_OFFSET.
 */
void boot_state_read(uint32_t bank_base, boot_state_t *out);

/**
 * boot_state_write() — Erase the boot-state sector and program @p st.
 *
 * Erases sector BOOT_STATE_SECTOR of the bank identified by @p bank_base,
 * then programs @p st at bank_base + BOOT_STATE_OFFSET.
 *
 * @param bank_base  Physical base address of the bank.
 * @param st         Record to write.
 */
void boot_state_write(uint32_t bank_base, const boot_state_t *st);

/**
 * boot_state_mark_pending() — Set the boot-state to "pending confirmation".
 *
 * Writes magic=BOOT_STATE_MAGIC, pending=1, attempts=0.
 * Call on the INACTIVE bank just before flash_set_swap_and_reset().
 *
 * @param bank_base  Physical base address of the bank.
 */
void boot_state_mark_pending(uint32_t bank_base);

/**
 * boot_state_clear() — Erase the boot-state sector (clears the pending flag).
 *
 * After erase the sector reads as all-0xFF; magic will not match
 * BOOT_STATE_MAGIC so the bank is treated as confirmed/good on next read.
 *
 * @param bank_base  Physical base address of the bank.
 */
void boot_state_clear(uint32_t bank_base);

/**
 * boot_state_bump_attempts() — Increment the attempts counter.
 *
 * Reads the current record, increments attempts, writes back.
 *
 * @param bank_base  Physical base address of the bank.
 */
void boot_state_bump_attempts(uint32_t bank_base);

/* ---------------------------------------------------------------------------
 * Pure decision function (host-testable, no flash I/O)
 * ------------------------------------------------------------------------- */

/** Return values for boot_state_decide(). */
typedef enum {
    BOOT_DECISION_JUMP         = 0, /**< Bank confirmed; jump to app.        */
    BOOT_DECISION_BUMP_AND_JUMP = 1, /**< Bump attempts counter, then jump.  */
    BOOT_DECISION_ROLLBACK     = 2  /**< Too many attempts; rollback.         */
} boot_decision_t;

/**
 * boot_state_decide() — Pure, host-testable boot-state decision function.
 *
 * Given the current record and the configured maximum, returns what the
 * bootloader should do.  Contains NO flash I/O; suitable for unit tests.
 *
 * @param st            Current boot_state_t read from flash.
 * @param max_attempts  Maximum number of allowed attempts.
 * @return BOOT_DECISION_JUMP, BOOT_DECISION_BUMP_AND_JUMP, or
 *         BOOT_DECISION_ROLLBACK.
 */
boot_decision_t boot_state_decide(const boot_state_t *st, uint32_t max_attempts);

#endif /* BOOT_STATE_H */
