/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file boot_confirm.h
 * @brief App-side boot-confirmation helper.
 *
 * A freshly-activated app must call boot_confirm() once it is healthy.
 * The bootloader counts each boot attempt; if the app is never confirmed
 * within MAX_BOOT_ATTEMPTS attempts the bootloader rolls back to the
 * previous (known-good) bank.
 *
 * Implementation:
 *   Erases the active bank's boot-state sector at BOOT_STATE_SECTOR_ADDR
 *   (0x08016000 under the current bank mapping).  After erase the sector
 *   reads as all-0xFF, which the bootloader treats as "confirmed/good".
 *
 * Limitations (do not fix for this simulation example):
 *   1. On real H5 silicon, flash operations must execute from RAM/ITCM
 *      when operating on the bank the CPU is executing from (RM0481
 *      §7.3.4 "Read-while-write" constraint).  In the LabWired H563
 *      simulator this constraint is not enforced; the erase completes
 *      correctly while executing from flash.
 *   2. The erase + implicit "clear" is not atomic across power loss.
 *      If the MCU loses power mid-erase the sector may be partially
 *      erased; on the next boot the magic word will not match, so the
 *      bank is treated as confirmed (safe default).
 */

#ifndef BOOT_CONFIRM_H
#define BOOT_CONFIRM_H

/**
 * boot_confirm() — Confirm that this app is healthy.
 *
 * Erases the active bank's boot-state sector, clearing the pending flag.
 * Must be called once the app has completed its self-checks.
 *
 * Safe to call more than once (erasing an already-erased sector is a no-op
 * on H5 silicon and in the simulator).
 */
void boot_confirm(void);

#endif /* BOOT_CONFIRM_H */
