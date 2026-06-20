ENTRY(Reset)
MEMORY {
  BL   (rx)  : ORIGIN = 0x08000000, LENGTH = 96K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 640K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_app_bank1       = 0x08018000;           /* App A: active bank app region   */
_app_bank2       = 0x08118000;           /* App B: inactive bank / OTA dest */
_app_region_size = 0x100000 - 0x18000;  /* 928 KB                           */
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
