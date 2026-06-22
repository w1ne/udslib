/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state_test.c
 * @brief Host unit test for the boot-state decision state machine.
 *
 * Tests the pure boot_state_decide() function (no flash I/O, no MCU deps).
 *
 * Build and run (host):
 *   gcc -O2 -g -I. -Wall -Wextra -Werror \
 *       boot_state_test.c boot_state_stub.c -o bootstate_test
 *   ./bootstate_test
 *
 * The Makefile exposes this as:
 *   make -C bootloader bootstate-test
 */

#include "boot_state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Minimal CHECK macro
 * ------------------------------------------------------------------------- */
#define CHECK(cond, msg)                          \
    do {                                          \
        if (!(cond)) {                            \
            fprintf(stderr, "FAIL: %s\n", (msg)); \
            return 1;                             \
        }                                         \
        printf("PASS: %s\n", (msg));              \
    } while (0)

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static boot_state_t make_pending(uint32_t attempts)
{
    boot_state_t st;
    st.magic = BOOT_STATE_MAGIC;
    st.pending = 1u;
    st.attempts = attempts;
    st.reserved = 0xFFFFFFFFu;
    return st;
}

static boot_state_t make_confirmed(void)
{
    boot_state_t st;
    st.magic = BOOT_STATE_MAGIC;
    st.pending = 0u;
    st.attempts = 0u;
    st.reserved = 0xFFFFFFFFu;
    return st;
}

static boot_state_t make_erased(void)
{
    boot_state_t st;
    /* All-0xFF — erased sector, magic does not match. */
    memset(&st, 0xFF, sizeof(st));
    return st;
}

/* ---------------------------------------------------------------------------
 * Slot-buffer helpers (mirror the on-flash attempt-slot region)
 * ------------------------------------------------------------------------- */

/* Build a slot buffer with `programmed` attempt slots marked, the rest erased
 * (0xFF). Slots are programmed in order, as on flash. */
static void make_slots(uint8_t *buf, uint32_t programmed)
{
    memset(buf, 0xFF, (size_t) BOOT_STATE_ATTEMPT_SLOTS * BOOT_STATE_SLOT_SIZE);
    for (uint32_t i = 0u; i < programmed && i < BOOT_STATE_ATTEMPT_SLOTS; i++) {
        uint32_t mark = ATTEMPT_SLOT_MARK;
        memcpy(buf + (i * BOOT_STATE_SLOT_SIZE), &mark, sizeof(mark));
    }
}

/* ---------------------------------------------------------------------------
 * Test: not-pending bank → JUMP
 * ------------------------------------------------------------------------- */
static int test_confirmed_jumps(void)
{
    boot_state_t st = make_confirmed();
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP, "confirmed bank → JUMP");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: erased sector (all-0xFF) → JUMP (safe default)
 * ------------------------------------------------------------------------- */
static int test_erased_jumps(void)
{
    boot_state_t st = make_erased();
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP,
          "erased sector → JUMP (safe default)");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: pending, attempts=0 → BUMP_AND_JUMP
 * ------------------------------------------------------------------------- */
static int test_pending_first_attempt(void)
{
    boot_state_t st = make_pending(0u);
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_BUMP_AND_JUMP,
          "pending attempts=0 → BUMP_AND_JUMP");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: pending, attempts = MAX-1 → BUMP_AND_JUMP (last chance)
 * ------------------------------------------------------------------------- */
static int test_pending_last_chance(void)
{
    boot_state_t st = make_pending(MAX_BOOT_ATTEMPTS - 1u);
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_BUMP_AND_JUMP,
          "pending attempts=MAX-1 → BUMP_AND_JUMP (last chance)");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: pending, attempts = MAX → ROLLBACK
 * ------------------------------------------------------------------------- */
static int test_pending_exhausted_rollback(void)
{
    boot_state_t st = make_pending(MAX_BOOT_ATTEMPTS);
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_ROLLBACK,
          "pending attempts=MAX → ROLLBACK");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: pending, attempts > MAX → ROLLBACK (overflow/corruption safety)
 * ------------------------------------------------------------------------- */
static int test_pending_over_max_rollback(void)
{
    boot_state_t st = make_pending(MAX_BOOT_ATTEMPTS + 5u);
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_ROLLBACK,
          "pending attempts>MAX → ROLLBACK (corruption safety)");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: simulate full A/B rollback sequence
 *
 * Step 1: Bank A active, confirmed → JUMP
 * Step 2: Activate bank B (mark B pending, attempts=0)
 * Step 3: Boot B attempt 1 → BUMP_AND_JUMP (attempts becomes 1)
 * Step 4: Boot B attempt 2 → BUMP_AND_JUMP (attempts becomes 2)
 * Step 5: Boot B attempt 3 → BUMP_AND_JUMP (attempts becomes 3)
 *         (MAX_BOOT_ATTEMPTS = 3; this was the last chance)
 *         (WAIT — after bump attempts=3, next read sees attempts=3 >= MAX)
 * Step 6: Boot B attempt 4 → ROLLBACK
 * Step 7: After rollback, bank A confirmed → JUMP
 * ------------------------------------------------------------------------- */
static int test_full_rollback_sequence(void)
{
    /* Step 1: Bank A confirmed. */
    {
        boot_state_t st = make_confirmed();
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP,
              "seq: bank A confirmed → JUMP");
    }

    /* Steps 2-5: Bank B pending, work through attempts 0..MAX-1. */
    for (uint32_t i = 0u; i < MAX_BOOT_ATTEMPTS; i++) {
        boot_state_t st = make_pending(i);
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_BUMP_AND_JUMP,
              "seq: bank B pending, attempts<MAX → BUMP_AND_JUMP");
    }

    /* Step 6: Attempts counter has reached MAX — rollback. */
    {
        boot_state_t st = make_pending(MAX_BOOT_ATTEMPTS);
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_ROLLBACK,
              "seq: bank B attempts=MAX → ROLLBACK");
    }

    /* Step 7: Back on bank A, cleared/confirmed. */
    {
        boot_state_t st = make_erased(); /* boot_state_clear() leaves all-0xFF */
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP,
              "seq: bank A after rollback (erased) → JUMP");
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: 0xFF03 PerformRollback boot-state semantics
 *
 * The routine clears the other bank's boot-state before swapping.
 * After boot_state_clear() the sector is all-0xFF, so boot_state_decide()
 * returns BOOT_DECISION_JUMP (confirmed / no pending flag).
 *
 * This test validates the post-rollback invariant: the bank we rolled back
 * to must not appear pending to the next boot decision.
 * ------------------------------------------------------------------------- */
static int test_perform_rollback_bootstate_invariant(void)
{
    /*
     * Simulate the other bank's boot-state as erased (all-0xFF), which is
     * what boot_state_clear() leaves behind.  The next boot on that bank
     * must decide JUMP — not ROLLBACK or BUMP_AND_JUMP.
     */
    boot_state_t st = make_erased();
    CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP,
          "0xFF03: cleared other-bank boot-state → JUMP on next boot");

    /*
     * Also verify the case where the other bank was previously confirmed
     * (magic present, pending=0) — also safe, also JUMP.
     */
    boot_state_t st2 = make_confirmed();
    CHECK(boot_state_decide(&st2, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_JUMP,
          "0xFF03: confirmed other-bank boot-state → JUMP on next boot");

    /*
     * Guard: if somehow the other bank's state appears pending (should not
     * happen after a clean rollback path, but test the FSM correctness), the
     * bootloader would detect it and roll back again rather than boot-looping
     * silently.
     */
    boot_state_t st3 = make_pending(MAX_BOOT_ATTEMPTS);
    CHECK(boot_state_decide(&st3, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_ROLLBACK,
          "0xFF03 guard: pending+exhausted other-bank still → ROLLBACK (FSM correct)");

    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: boot_state_count_attempts counts programmed slots in order
 * ------------------------------------------------------------------------- */
static int test_count_attempts_basic(void)
{
    uint8_t buf[BOOT_STATE_ATTEMPT_SLOTS * BOOT_STATE_SLOT_SIZE];

    make_slots(buf, 0u);
    CHECK(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS) == 0u,
          "count: 0 programmed slots → 0 attempts (all erased)");

    make_slots(buf, 1u);
    CHECK(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS) == 1u,
          "count: 1 programmed slot → 1 attempt");

    make_slots(buf, BOOT_STATE_ATTEMPT_SLOTS);
    CHECK(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS) == BOOT_STATE_ATTEMPT_SLOTS,
          "count: all slots programmed → MAX attempts");

    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: counting stops at the first erased slot (in-order programming)
 *
 * A still-erased slot means no further attempts were recorded; a gap must not
 * be counted. This also models a TORN attempt-slot program: the half-written
 * (commits-nothing) slot reads all-0xFF, so it is NOT counted — the prior
 * count is preserved (atomic quad-word property).
 * ------------------------------------------------------------------------- */
static int test_count_attempts_torn_slot_not_counted(void)
{
    uint8_t buf[BOOT_STATE_ATTEMPT_SLOTS * BOOT_STATE_SLOT_SIZE];

    /* 1 programmed, then an erased ("torn") slot, then a stray programmed one:
     * counting must stop at the erased slot and report exactly 1. */
    make_slots(buf, 1u);
    if (BOOT_STATE_ATTEMPT_SLOTS >= 3u) {
        uint32_t mark = ATTEMPT_SLOT_MARK;
        memcpy(buf + (2u * BOOT_STATE_SLOT_SIZE), &mark,
               sizeof(mark)); /* slot 2 set, slot 1 erased */
    }
    CHECK(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS) == 1u,
          "count: torn/erased slot stops the count (prior count preserved)");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: torn/partial boot-state never yields "confirmed" for a pending bank
 *
 * Reconstruct the abstract record the way the flash reader does (header magic
 * + pending, attempts from the slot count) and assert that across the whole
 * range of slot counts a pending bank is NEVER decided as a plain JUMP
 * ("confirmed"): it is BUMP_AND_JUMP while budget remains, then ROLLBACK.
 * A power loss can only (a) fail to add a slot → fewer attempts → still
 * on-trial, or (b) leave the pending header committed → still on-trial.
 * It can never flip a pending bank to confirmed.
 * ------------------------------------------------------------------------- */
static int test_torn_pending_never_confirmed(void)
{
    uint8_t buf[BOOT_STATE_ATTEMPT_SLOTS * BOOT_STATE_SLOT_SIZE];

    for (uint32_t programmed = 0u; programmed <= BOOT_STATE_ATTEMPT_SLOTS; programmed++) {
        make_slots(buf, programmed);

        boot_state_t st;
        st.magic = BOOT_STATE_MAGIC;
        st.pending = 1u;
        st.reserved = 0xFFFFFFFFu;
        st.attempts = boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS);

        boot_decision_t d = boot_state_decide(&st, MAX_BOOT_ATTEMPTS);
        CHECK(d != BOOT_DECISION_JUMP, "torn pending bank is never decided as confirmed (JUMP)");
        if (st.attempts < MAX_BOOT_ATTEMPTS) {
            CHECK(d == BOOT_DECISION_BUMP_AND_JUMP, "pending + budget remaining → BUMP_AND_JUMP");
        }
        else {
            CHECK(d == BOOT_DECISION_ROLLBACK, "pending + budget exhausted → ROLLBACK");
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: full no-erase trial sequence (slots drive the count)
 *
 * Pre-erased sector + pending header. Each trial boot programs ONE slot.
 * Decision walks BUMP_AND_JUMP (slots 0..MAX-1) then ROLLBACK (slots == MAX).
 * ------------------------------------------------------------------------- */
static int test_slot_driven_trial_sequence(void)
{
    uint8_t buf[BOOT_STATE_ATTEMPT_SLOTS * BOOT_STATE_SLOT_SIZE];

    for (uint32_t programmed = 0u; programmed < MAX_BOOT_ATTEMPTS; programmed++) {
        make_slots(buf, programmed);
        boot_state_t st = make_pending(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS));
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_BUMP_AND_JUMP,
              "slot-seq: pending, slots<MAX → BUMP_AND_JUMP");
    }

    make_slots(buf, MAX_BOOT_ATTEMPTS);
    {
        boot_state_t st = make_pending(boot_state_count_attempts(buf, BOOT_STATE_ATTEMPT_SLOTS));
        CHECK(boot_state_decide(&st, MAX_BOOT_ATTEMPTS) == BOOT_DECISION_ROLLBACK,
              "slot-seq: pending, slots==MAX → ROLLBACK");
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */
int main(void)
{
    int rc = 0;
    rc |= test_confirmed_jumps();
    rc |= test_erased_jumps();
    rc |= test_pending_first_attempt();
    rc |= test_pending_last_chance();
    rc |= test_pending_exhausted_rollback();
    rc |= test_pending_over_max_rollback();
    rc |= test_full_rollback_sequence();
    rc |= test_perform_rollback_bootstate_invariant();
    rc |= test_count_attempts_basic();
    rc |= test_count_attempts_torn_slot_not_counted();
    rc |= test_torn_pending_never_confirmed();
    rc |= test_slot_driven_trial_sequence();

    if (rc == 0) {
        printf("\nAll bootstate-test cases PASS\n");
    }
    else {
        fprintf(stderr, "\nSome bootstate-test cases FAILED\n");
    }
    return rc;
}
