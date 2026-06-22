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
 *   Erases the running bank's boot-state sector (sector 11).  The erase
 *   TARGET is selected by NSCR.BKSEL by physical bank (read from
 *   OPTSR_CUR.SWAP_BANK), not by the SWAP_BANK-remapped read view — so the
 *   sector of the bank actually executing is cleared.  After erase the sector
 *   reads as all-0xFF, which the bootloader treats as "confirmed/good".
 *
 * Read-while-write:
 *   On real H5 silicon, flash operations must execute from RAM when
 *   operating on the bank the CPU is executing from (RM0481 §7.3.4
 *   "Read-while-write" constraint).  boot_confirm() is marked RAMFUNC and
 *   copied to SRAM by startup.c before main() runs (hence before this is
 *   called), so the erase and its NSSR.BSY poll run from RAM.
 *
 * Power-loss behaviour:
 *   The confirm erase is the LAST step of a successful boot. If the MCU loses
 *   power mid-erase the sector may stay (partly) programmed: the pending
 *   header survives, so the bank simply remains on-trial and the bootloader
 *   keeps its rollback budget on the next boot — never a spurious "confirmed".
 *   Only a fully completed erase clears the pending flag. This is the safe
 *   direction: a torn confirm costs at most one extra trial boot.
 */

#ifndef BOOT_CONFIRM_H
#define BOOT_CONFIRM_H

/*
 * RAMFUNC — execute boot_confirm() from SRAM.  It erases the active bank's
 * boot-state sector, which on H5 silicon must not run from the bank being
 * erased (read-while-write hazard, RM0481 §7.3.4).  startup.c copies the
 * .ramfunc section to RAM before main() (hence before this is called); the
 * long_call attribute lets the flash-resident caller reach it.  Enabled only
 * for the ARM firmware build — empty otherwise so any host build stays clean.
 */
#if defined(__arm__)
#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))
#else
#define RAMFUNC
#endif

/**
 * boot_confirm() — Confirm that this app is healthy.
 *
 * Erases the active bank's boot-state sector, clearing the pending flag.
 * Must be called once the app has completed its self-checks.
 *
 * Safe to call more than once (erasing an already-erased sector is a no-op
 * on H5 silicon and in the simulator).
 */
RAMFUNC void boot_confirm(void);

#endif /* BOOT_CONFIRM_H */
