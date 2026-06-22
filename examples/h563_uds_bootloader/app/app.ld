/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * app.ld — linker script for the demo App A / App B payloads.
 *
 * The 0x400-byte OTA image header at 0x08018000..0x08018400 is NOT part of
 * the application link; mkimage.py prepends it.  The app therefore links
 * its vector table at 0x08018400 (= bank1_base + 0x18000 + OTA_IMAGE_HDR_SIZE).
 *
 * SWAP_BANK note: the active bank ALWAYS appears at 0x08000000.
 * Both App A and App B are therefore linked at the SAME address.  The OTA
 * flow downloads the image to the inactive bank (physical 0x08118000);
 * after SWAP_BANK that physical address maps to 0x08000000, so the code
 * at 0x08118400 executes at its link address 0x08018400.
 *
 * FLASH region: skip the bootloader region (96 KB = 0x18000) and the
 * 0x400-byte header, leaving 0xE7C00 bytes for the payload.
 *   0x100000 (bank size) - 0x18000 (BL region) - 0x400 (header) = 0xE7C00
 */

ENTRY(Reset)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x08018400, LENGTH = 0xE7C00
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 640K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS {
    /* Vector table must be first in flash so VTOR points here. */
    .isr_vector : {
        KEEP(*(.isr_vector))
    } > FLASH

    /* Code and read-only data */
    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    /* Initialised data: stored in FLASH, copied to RAM at startup. */
    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /*
     * RAM-resident code: boot_confirm() erases the active bank's boot-state
     * sector, which on H5 silicon must NOT execute from the bank being erased
     * (read-while-write hazard, RM0481 §7.3.4).  Linked for execution in RAM
     * (VMA) but stored in FLASH (LMA, after .data's load image); startup.c
     * copies it before main(), exactly like .data.
     */
    _siramfunc = LOADADDR(.ramfunc);
    .ramfunc : {
        . = ALIGN(4);
        _sramfunc = .;
        *(.ramfunc*)
        . = ALIGN(4);
        _eramfunc = .;
    } > RAM AT > FLASH

    /* Zero-initialised data */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    . = ALIGN(8);
    PROVIDE(end  = .);
    PROVIDE(_end = .);

    /DISCARD/ : { *(.ARM.exidx*) *(.note.gnu.build-id*) }
}
