/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * startup.c — minimal Cortex-M33 startup for the demo app payload.
 *
 * Freestanding — uses only <stdint.h>; no libc, no shims.
 * The linker script places .isr_vector at app_base + OTA_IMAGE_HDR_SIZE
 * (= 0x08018010) so SCB->VTOR can be pointed here after the OTA jump.
 */

#include <stdint.h>

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _siramfunc;
extern uint32_t _sramfunc;
extern uint32_t _eramfunc;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

/* Forward declaration of application entry point. */
extern int main(void);

static void Default_Handler(void)
{
    for (;;) {
    }
}

void Reset(void)
{
    /* Copy initialised data from flash load address to RAM. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Copy RAM-resident flash routines (.ramfunc) from flash (LMA) to RAM
     * (VMA) BEFORE main() — main() calls boot_confirm(), which erases the
     * active bank and must run from SRAM (H5 read-while-write). */
    src = &_siramfunc;
    for (dst = &_sramfunc; dst < &_eramfunc;) {
        *dst++ = *src++;
    }

    /* Zero-initialise BSS. */
    for (dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0u;
    }

    (void) main();

    /* Should never return; spin if it does. */
    for (;;) {
    }
}

/*
 * Vector table — placed at .isr_vector (= FLASH ORIGIN = 0x08018010).
 *
 * Slot 0:  initial MSP  (_estack, top of 640 KB RAM at 0x200A0000)
 * Slot 1:  Reset handler
 * Slot 2:  NMI
 * Slot 3:  HardFault
 * Remaining slots unused — Default_Handler keeps the core halted on any fault.
 */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void)) &_estack,  /* 0: initial MSP */
    Reset,                       /* 1: Reset */
    Default_Handler,             /* 2: NMI */
    Default_Handler,             /* 3: HardFault */
    Default_Handler,             /* 4: MemManage */
    Default_Handler,             /* 5: BusFault */
    Default_Handler,             /* 6: UsageFault */
    0,                           /* 7: reserved */
    0,                           /* 8: reserved */
    0,                           /* 9: reserved */
    0,                           /* 10: reserved */
    Default_Handler,             /* 11: SVCall */
    Default_Handler,             /* 12: DebugMon */
    0,                           /* 13: reserved */
    Default_Handler,             /* 14: PendSV */
    Default_Handler,             /* 15: SysTick */
};
