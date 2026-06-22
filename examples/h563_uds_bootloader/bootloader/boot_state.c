/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state.c
 * @brief Per-bank boot-confirmation flag and rollback counter.
 *
 * See boot_state.h for the full design rationale, wear/atomicity notes,
 * and the same-bank-erase simulator caveat.
 */

#include "boot_state.h"
#include "flash_h5.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static uint32_t sector_addr(uint32_t bank_base)
{
    return bank_base + BOOT_STATE_OFFSET;
}

/* Return 1 if bank_base is bank 2 (physical 0x08100000), 0 for bank 1. */
static uint8_t bank_index(uint32_t bank_base)
{
    return (bank_base >= 0x08100000UL) ? 1u : 0u;
}

/* ---------------------------------------------------------------------------
 * boot_state_read
 * ------------------------------------------------------------------------- */
void boot_state_read(uint32_t bank_base, boot_state_t *out)
{
    const boot_state_t *flash_rec =
        (const boot_state_t *) (uintptr_t) sector_addr(bank_base);
    memcpy(out, flash_rec, sizeof(*out));
}

/* ---------------------------------------------------------------------------
 * boot_state_write
 *
 * Erase the 8 KB boot-state sector then program the 16-byte struct.
 * The struct is 16 bytes = one H5 quad-word, so no staging is needed.
 * ------------------------------------------------------------------------- */
void boot_state_write(uint32_t bank_base, const boot_state_t *st)
{
    flash_unlock();
    flash_erase_sector(bank_index(bank_base), BOOT_STATE_SECTOR);
    flash_program(sector_addr(bank_base), (const uint8_t *) st, sizeof(boot_state_t));
}

/* ---------------------------------------------------------------------------
 * boot_state_mark_pending
 * ------------------------------------------------------------------------- */
void boot_state_mark_pending(uint32_t bank_base)
{
    boot_state_t st;
    st.magic    = BOOT_STATE_MAGIC;
    st.pending  = 1u;
    st.attempts = 0u;
    st.reserved = 0xFFFFFFFFu;
    boot_state_write(bank_base, &st);
}

/* ---------------------------------------------------------------------------
 * boot_state_clear
 * ------------------------------------------------------------------------- */
void boot_state_clear(uint32_t bank_base)
{
    flash_unlock();
    flash_erase_sector(bank_index(bank_base), BOOT_STATE_SECTOR);
    /* Sector now reads all-0xFF; magic will not match BOOT_STATE_MAGIC. */
}

/* ---------------------------------------------------------------------------
 * boot_state_bump_attempts
 * ------------------------------------------------------------------------- */
void boot_state_bump_attempts(uint32_t bank_base)
{
    boot_state_t st;
    boot_state_read(bank_base, &st);
    st.attempts += 1u;
    boot_state_write(bank_base, &st);
}

/* ---------------------------------------------------------------------------
 * boot_state_decide  (pure, no flash I/O — host-testable)
 * ------------------------------------------------------------------------- */
boot_decision_t boot_state_decide(const boot_state_t *st, uint32_t max_attempts)
{
    /* Not pending (or no valid record): bank is confirmed. */
    if (st->magic != BOOT_STATE_MAGIC || st->pending == 0u) {
        return BOOT_DECISION_JUMP;
    }

    /* Pending and attempts exhausted: rollback. */
    if (st->attempts >= max_attempts) {
        return BOOT_DECISION_ROLLBACK;
    }

    /* Pending but still within attempt budget: count this attempt. */
    return BOOT_DECISION_BUMP_AND_JUMP;
}
