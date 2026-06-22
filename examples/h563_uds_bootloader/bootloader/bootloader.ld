ENTRY(Reset)
MEMORY {
  BL   (rx)  : ORIGIN = 0x08000000, LENGTH = 96K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 640K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_app_bank1       = 0x08018000;           /* App A: active bank app region   */
_app_bank2       = 0x08118000;           /* App B: inactive bank / OTA dest */
_app_region_size = 0x100000 - 0x18000;  /* 928 KB                           */
/*
 * Boot-state sector: sector 11 of each bank (bank-relative offset 0x16000).
 * BL code is ~27 KB and safely ends below 0x08016000 (= 90 KB into bank 1).
 * The sector is reserved for the boot-confirmation flag; BL code never
 * reaches it.  Under SWAP_BANK the active bank always maps to 0x08000000,
 * so the active bank's boot-state is always at _boot_state_addr.
 */
_boot_state_addr  = 0x08016000;         /* Bank 1 view: boot-state sector   */
_boot_state_addr2 = 0x08116000;         /* Bank 2 view: boot-state sector   */
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > BL
  .text       : { *(.text*) *(.rodata*) } > BL
  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > BL
  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM
  . = ALIGN(8);
  PROVIDE(end = .);
  PROVIDE(_end = .);
  /DISCARD/ : { *(.ARM.exidx*) *(.note.gnu.build-id*) }
}
/* Hard-fail the link if BL code/rodata/data-init would grow into the reserved
 * boot-state sector at 0x08016000, rather than silently corrupting it. */
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= _boot_state_addr,
       "Bootloader flash image overflows into the boot-state sector (0x16000)")
