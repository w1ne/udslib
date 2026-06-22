/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state.c
 * @brief Per-bank boot-confirmation flag and rollback counter.
 *
 * See boot_state.h for the full design rationale: torn-write safety via a
 * single pre-erased atomic quad-word program on the activate/boot path, the
 * header-slot + attempt-slot on-flash layout, and the same-bank-erase caveat.
 */

#include "boot_state.h"
#include "flash_h5.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

RAMFUNC static uint32_t sector_addr(uint32_t bank_base)
{
    return bank_base + BOOT_STATE_OFFSET;
}

/* Address of the first attempt slot (header slot is slot 0). */
RAMFUNC static uint32_t attempt_slot_base(uint32_t bank_base)
{
    return sector_addr(bank_base) + BOOT_STATE_SLOT_SIZE;
}

/* Return 1 if bank_base is bank 2 (physical 0x08100000), 0 for bank 1. */
RAMFUNC static uint8_t bank_index(uint32_t bank_base)
{
    return (bank_base >= 0x08100000UL) ? 1u : 0u;
}

/* ---------------------------------------------------------------------------
 * boot_state_count_attempts  (pure, no flash I/O — host-testable)
 * ------------------------------------------------------------------------- */
uint32_t boot_state_count_attempts(const uint8_t *slots, uint32_t max_slots)
{
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < max_slots; i++) {
        uint32_t first_word;
        memcpy(&first_word, slots + (i * BOOT_STATE_SLOT_SIZE), sizeof(first_word));
        if (first_word == 0xFFFFFFFFu) {
            /* Still erased: attempt slots are programmed in order, so no
             * further attempts were recorded. A torn (all-0xFF) slot is
             * treated as "no attempt", which is the safe behaviour. */
            break;
        }
        count++;
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * boot_state_read
 *
 * Reconstruct the abstract record from the header slot + counted attempt slots.
 * ------------------------------------------------------------------------- */
void boot_state_read(uint32_t bank_base, boot_state_t *out)
{
    const boot_state_t *hdr =
        (const boot_state_t *) (uintptr_t) sector_addr(bank_base);
    out->magic    = hdr->magic;
    out->pending  = hdr->pending;
    out->reserved = hdr->reserved;
    out->attempts = boot_state_count_attempts(
        (const uint8_t *) (uintptr_t) attempt_slot_base(bank_base),
        BOOT_STATE_ATTEMPT_SLOTS);
}

/* ---------------------------------------------------------------------------
 * boot_state_prepare
 *
 * Pre-erase the boot-state sector during the OTA ERASE phase so the later
 * activate/boot path is PROGRAM-ONLY (one atomic quad-word, no torn window).
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_state_prepare(uint32_t bank_base)
{
    flash_unlock();
    flash_erase_sector(bank_index(bank_base), BOOT_STATE_SECTOR);
}

/* ---------------------------------------------------------------------------
 * boot_state_mark_pending
 *
 * PROGRAM-ONLY: write the header slot into the already pre-erased sector.
 * One H5 quad-word (16 bytes) → atomic commit.
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_state_mark_pending(uint32_t bank_base)
{
    boot_state_t hdr;
    hdr.magic    = BOOT_STATE_MAGIC;
    hdr.pending  = 1u;
    hdr.attempts = 0xFFFFFFFFu; /* not stored in header; keep erased pattern */
    hdr.reserved = 0xFFFFFFFFu;

    flash_unlock();
    flash_program(sector_addr(bank_base), (const uint8_t *) &hdr, sizeof(hdr));
}

/* ---------------------------------------------------------------------------
 * boot_state_clear
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_state_clear(uint32_t bank_base)
{
    flash_unlock();
    flash_erase_sector(bank_index(bank_base), BOOT_STATE_SECTOR);
    /* Sector now reads all-0xFF; header magic will not match BOOT_STATE_MAGIC. */
}

/* ---------------------------------------------------------------------------
 * boot_state_bump_attempts
 *
 * PROGRAM-ONLY: program the next free attempt slot with ATTEMPT_SLOT_MARK.
 * No erase, no read-modify-write. A torn program commits nothing (atomic
 * quad-word), so the prior attempt count survives a power loss intact.
 * ------------------------------------------------------------------------- */
RAMFUNC void boot_state_bump_attempts(uint32_t bank_base)
{
    uint32_t used = boot_state_count_attempts(
        (const uint8_t *) (uintptr_t) attempt_slot_base(bank_base),
        BOOT_STATE_ATTEMPT_SLOTS);

    if (used >= BOOT_STATE_ATTEMPT_SLOTS) {
        /* No reserved slot left; the count already forces ROLLBACK. */
        return;
    }

    uint32_t slot[BOOT_STATE_SLOT_SIZE / sizeof(uint32_t)];
    slot[0] = ATTEMPT_SLOT_MARK;
    slot[1] = 0xFFFFFFFFu;
    slot[2] = 0xFFFFFFFFu;
    slot[3] = 0xFFFFFFFFu;

    flash_unlock();
    flash_program(attempt_slot_base(bank_base) + (used * BOOT_STATE_SLOT_SIZE),
                  (const uint8_t *) slot, sizeof(slot));
}

/* ---------------------------------------------------------------------------
 * boot_state_decide  (pure, no flash I/O — host-testable)
 * ------------------------------------------------------------------------- */
boot_decision_t boot_state_decide(const boot_state_t *st, uint32_t max_attempts)
{
    /* Not pending (or no valid header record): bank is confirmed. */
    if (st->magic != BOOT_STATE_MAGIC || st->pending != 1u) {
        return BOOT_DECISION_JUMP;
    }

    /* Pending and attempts exhausted: rollback. */
    if (st->attempts >= max_attempts) {
        return BOOT_DECISION_ROLLBACK;
    }

    /* Pending but still within attempt budget: count this attempt. */
    return BOOT_DECISION_BUMP_AND_JUMP;
}
