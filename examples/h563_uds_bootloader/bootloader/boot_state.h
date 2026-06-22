/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state.h
 * @brief Per-bank boot-confirmation flag and rollback counter.
 *
 * Torn-write-safe design
 * ----------------------
 * An H5 flash PROGRAM commits one full 16-byte quad-word ATOMICALLY: the
 * write buffer (NSSR.WBNE) only commits on the 16th byte (sets EOP).  A
 * partial/torn quad-word commits NOTHING — the target stays erased.  There is
 * no "half-written" quad-word.  This module relies on that: every operation on
 * the critical activate/boot path is a SINGLE pre-erased quad-word PROGRAM, so
 * a power loss can never leave an intermediate state that defeats rollback.
 *
 * The erase is hoisted OUT of the activate/boot path entirely:
 *   - The inactive bank's boot-state sector is pre-erased during the OTA ERASE
 *     phase (alongside erase_app_sectors()/0xFF00 in main.c), so by the time
 *     0xFF02 ActivateSoftware runs the sector is already all-0xFF.
 *   - boot_state_mark_pending() is then PROGRAM-ONLY (one header quad-word).
 *   - boot_state_bump_attempts() never erases; it PROGRAMs one additional
 *     pre-erased attempt slot.  A torn slot-program leaves the prior count
 *     intact, so a power loss can neither erase the pending state nor make an
 *     unconfirmed bank look confirmed.
 *
 * Flash layout (dual-bank, 8 KB sectors, 96 KB BL region per bank):
 *
 *   Sector 11 of each bank (bank-relative offset 0x16000, 8 KB):
 *     BOOT_STATE_OFFSET = 0x16000   (sector 11 × 8 KB)
 *
 *   This sector is the LAST sector of the 96 KB bootloader region
 *   (sectors 0–11, 0x00000–0x17FFF).  BL code tops out well below 0x16000
 *   (= 90 KB), leaving this sector free.
 *
 *   The sector swaps WITH its bank under hardware SWAP_BANK, so the
 *   active-bank state is always accessible at the canonical address
 *   BOOT_STATE_BANK1_ADDR (0x08016000).
 *
 *   On-flash slot layout within the sector (every slot is one 16-byte
 *   quad-word; all start out 0xFF after erase):
 *
 *     offset 0x000  HEADER slot   { magic, pending, reserved0, reserved1 }
 *     offset 0x010  attempt slot 0
 *     offset 0x020  attempt slot 1
 *     ...
 *     offset 0x010 + (N-1)*0x10   attempt slot N-1
 *
 *   "pending" lives in the header slot.  Each trial boot PROGRAMs one attempt
 *   slot with ATTEMPT_SLOT_MARK; the number of programmed (non-erased) attempt
 *   slots is the attempt count.  BOOT_STATE_ATTEMPT_SLOTS slots are reserved
 *   (>= MAX_BOOT_ATTEMPTS): once the count reaches MAX_BOOT_ATTEMPTS the boot
 *   decision is ROLLBACK and no further slot is needed.
 *
 * Atomicity note:
 *   mark_pending PROGRAMs the header BEFORE the swap+reset, into the INACTIVE
 *   bank.  A power loss during that program leaves the bank still inactive, so
 *   the old (good) bank stays active and boots — safe.  A bump PROGRAMs one
 *   attempt slot of the ACTIVE bank; a torn program commits nothing, so the
 *   prior count survives — the unconfirmed bank stays on-trial, never
 *   spuriously confirmed.
 *
 * Same-bank erase note (read-while-write):
 *   The bootloader erases/programs a sector of the bank it is executing from.
 *   On real H5 silicon this is safe ONLY when the flash op runs from RAM
 *   (RM0481 §7.3.4).  boot_state_clear()/boot_state_prepare()/program helpers
 *   and the flash driver they call are marked RAMFUNC and copied to SRAM by
 *   startup.c before main() (see flash_h5.h).
 */

#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stdint.h>

/* For the RAMFUNC attribute: the flash-touching boot_state_* routines execute
 * from SRAM on the ARM target (read-while-write hazard, see flash_h5.h).
 * Callers need the long_call attribute on the prototype to reach the
 * RAM-resident routine, so the prototypes below are annotated RAMFUNC. */
#include "flash_h5.h"

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** Magic value that marks a valid boot-state header slot. */
#define BOOT_STATE_MAGIC  0xB007A5A5u

/**
 * Bank-relative byte offset of the boot-state sector (sector 11 × 8 KB).
 * The sector covers [bank_base + 0x16000 .. bank_base + 0x18000).
 */
#define BOOT_STATE_OFFSET  0x16000UL

/**
 * Canonical flash address of the active-bank boot-state sector when no
 * SWAP_BANK is active (bank 1 view).  Under SWAP_BANK the hardware re-maps
 * so the active bank always appears at 0x08000000; the offset is the same
 * either way.
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

/**
 * Number of pre-erased attempt slots reserved after the header slot.
 * Must be >= MAX_BOOT_ATTEMPTS (we never need to program past MAX, since the
 * MAX-th counted attempt yields ROLLBACK).  The sector is 8 KB = 512 slots,
 * so this is comfortably within bounds.
 */
#define BOOT_STATE_ATTEMPT_SLOTS  MAX_BOOT_ATTEMPTS

/**
 * Marker programmed into an attempt slot's first word to count one attempt.
 * Any value other than the erased pattern (0xFFFFFFFF) marks the slot as
 * "used"; this fixed marker is used so the intent is explicit on a hex dump.
 */
#define ATTEMPT_SLOT_MARK  0xA77E3917u

/** Size of one slot (one H5 quad-word). */
#define BOOT_STATE_SLOT_SIZE  16u

/* ---------------------------------------------------------------------------
 * Data structure
 * ------------------------------------------------------------------------- */

/**
 * Boot-state HEADER record stored in the first slot of the boot-state sector.
 *
 * The "attempts" field carried by the old single-record scheme is gone: the
 * attempt count is now derived by counting programmed attempt slots (see
 * boot_state_count_attempts()).  This struct is kept the abstract record that
 * boot_state_decide() reasons over; its @c attempts member is filled in by the
 * flash reader from the counted slots before calling boot_state_decide().
 *
 * Size: 16 bytes (one quad-word, matching H5 flash program granularity).
 */
typedef struct {
    uint32_t magic;    /**< BOOT_STATE_MAGIC when the header slot is valid.  */
    uint32_t pending;  /**< 1 = unconfirmed (on-trial); 0 = good.            */
    uint32_t attempts; /**< Counted attempts (filled from slots by reader).  */
    uint32_t reserved; /**< Unused; all-ones in the erased/header slot.      */
} boot_state_t;

/* The header is programmed as a single H5 quad-word; adding a field would
 * break that 16-byte alignment assumption silently. Fail the build instead. */
_Static_assert(sizeof(boot_state_t) == 16u, "boot_state_t must stay 16 bytes (one H5 quad-word)");

/* ---------------------------------------------------------------------------
 * API — flash side (target only; host build uses stubs)
 * ------------------------------------------------------------------------- */

/**
 * boot_state_read() — Read and reconstruct the boot-state record for a bank.
 *
 * Reads the header slot and counts the programmed attempt slots, filling
 * @p out->magic / pending from the header and @p out->attempts from the slot
 * count.  No flash writes.
 *
 * @param bank_base  Physical base address of the bank (0x08000000 or
 *                   0x08100000).
 * @param out        Output record.
 */
void boot_state_read(uint32_t bank_base, boot_state_t *out);

/**
 * boot_state_prepare() — Erase the boot-state sector (OTA ERASE phase).
 *
 * Pre-erases sector BOOT_STATE_SECTOR of @p bank_base so the later
 * boot_state_mark_pending() and per-boot attempt-slot programs are
 * PROGRAM-ONLY (no erase on the activate/boot critical path).  Call this for
 * the INACTIVE bank during the same phase that erases the inactive app region.
 *
 * @param bank_base  Physical base address of the bank.
 */
RAMFUNC void boot_state_prepare(uint32_t bank_base);

/**
 * boot_state_mark_pending() — Mark a (pre-erased) bank pending confirmation.
 *
 * PROGRAM-ONLY: writes the header slot { magic, pending=1, 0xFF, 0xFF } into
 * the already-erased sector (see boot_state_prepare()).  One atomic quad-word.
 * Call on the INACTIVE bank just before flash_swap_to_bank_and_reset().
 *
 * @param bank_base  Physical base address of the bank.
 */
RAMFUNC void boot_state_mark_pending(uint32_t bank_base);

/**
 * boot_state_clear() — Erase the boot-state sector (clears pending + attempts).
 *
 * After erase the sector reads all-0xFF; the header magic will not match, so
 * the bank is treated as confirmed/good on next read.
 *
 * @param bank_base  Physical base address of the bank.
 */
RAMFUNC void boot_state_clear(uint32_t bank_base);

/**
 * boot_state_bump_attempts() — Record one trial boot, WITHOUT erasing.
 *
 * PROGRAM-ONLY: programs the next free attempt slot with ATTEMPT_SLOT_MARK.
 * No read-modify-erase-rewrite.  A torn program commits nothing, leaving the
 * prior count intact.
 *
 * @param bank_base  Physical base address of the bank.
 */
RAMFUNC void boot_state_bump_attempts(uint32_t bank_base);

/* ---------------------------------------------------------------------------
 * Pure, host-testable helpers (no flash I/O)
 * ------------------------------------------------------------------------- */

/** Return values for boot_state_decide(). */
typedef enum {
    BOOT_DECISION_JUMP         = 0, /**< Bank confirmed; jump to app.        */
    BOOT_DECISION_BUMP_AND_JUMP = 1, /**< Bump attempts counter, then jump.  */
    BOOT_DECISION_ROLLBACK     = 2  /**< Too many attempts; rollback.         */
} boot_decision_t;

/**
 * boot_state_count_attempts() — Count programmed attempt slots (pure).
 *
 * Walks @p slots (each BOOT_STATE_SLOT_SIZE bytes) and counts those whose
 * first word is NOT the erased pattern 0xFFFFFFFF.  Counting stops at the
 * first still-erased slot: attempt slots are programmed in order, so an erased
 * slot means no further attempts were recorded.  A torn (all-0xFF) slot is
 * therefore counted as "not an attempt", which is the safe behaviour.
 *
 * @param slots      Pointer to the first attempt slot (header slot excluded).
 * @param max_slots  Number of reserved attempt slots to scan.
 * @return Number of programmed attempt slots in [0, max_slots].
 */
uint32_t boot_state_count_attempts(const uint8_t *slots, uint32_t max_slots);

/**
 * boot_state_decide() — Pure boot-state decision function (no flash I/O).
 *
 * @param st            Current boot_state_t (magic/pending from the header
 *                      slot, attempts from boot_state_count_attempts()).
 * @param max_attempts  Maximum number of allowed attempts.
 * @return BOOT_DECISION_JUMP, BOOT_DECISION_BUMP_AND_JUMP, or
 *         BOOT_DECISION_ROLLBACK.
 */
boot_decision_t boot_state_decide(const boot_state_t *st, uint32_t max_attempts);

#endif /* BOOT_STATE_H */
