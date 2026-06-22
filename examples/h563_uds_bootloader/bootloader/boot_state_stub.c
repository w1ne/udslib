/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_state_stub.c
 * @brief Host build stubs for flash functions used by boot_state.c.
 *
 * boot_state_test only exercises boot_state_decide() (pure, no I/O).
 * These stubs satisfy the linker without pulling in MCU-specific flash code.
 */

#include "flash_h5.h"
#include <string.h>

/* Host stub: no-op unlock. */
void flash_unlock(void) {}

/* Host stub: no-op erase. */
int flash_erase_sector(uint8_t bank, uint32_t sector)
{
    (void) bank;
    (void) sector;
    return 0;
}

/* Host stub: no-op program. */
int flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    (void) addr;
    (void) data;
    (void) len;
    return 0;
}

/* Host stub: always bank 0. */
uint8_t flash_active_bank(void)
{
    return 0u;
}

/* Host stub: no-op swap+reset (should never be called from tests). */
void flash_set_swap_and_reset(void)
{
    /* Should not be called in host test context. */
    for (;;) {}
}

/* Host stub: no-op explicit-bank swap+reset (should never be called from tests). */
void flash_swap_to_bank_and_reset(uint8_t target_bank)
{
    (void) target_bank;
    /* Should not be called in host test context. */
    for (;;) {}
}
