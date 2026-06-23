/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "app_jump.h"
#include "ota_image.h"
#include "image_validate.h"
#include <stddef.h>
#include <stdint.h>

/* SCB->VTOR register (Cortex-M33 System Control Block) */
#define SCB_VTOR (*(volatile uint32_t *) 0xE000ED08UL)

/* Size of a bank's app region: BANK_SIZE(1MB) - BL_REGION_SIZE(96KB).
 * This is the addressable window from app_base; image_validate() uses it to
 * confirm the header + payload + signature all fit inside the region. */
#define APP_REGION_SIZE (0x100000UL - 0x18000UL)

int app_is_valid(uint32_t app_base)
{
    /* Thin wrapper over the shared, host-testable validation core. The flash
     * app region is directly memory-mapped, so the image base doubles as a
     * byte buffer. APP_REGION_SIZE bounds the available bytes. */
    return image_validate((const uint8_t *) (uintptr_t) app_base, APP_REGION_SIZE);
}

__attribute__((noreturn)) void app_jump(uint32_t app_base)
{
    uint32_t vt = app_base + OTA_IMAGE_HDR_SIZE;

    /* Load MSP and reset-handler address from the vector table. */
    uint32_t sp = *((const uint32_t *) (uintptr_t) vt);
    uint32_t pc = *((const uint32_t *) (uintptr_t) (vt + 4u));

    /* Disable interrupts FIRST so no exception can fetch from a half-relocated
     * vector table (ARM Cortex-M33 TRM order: cpsid -> write VTOR -> DSB/ISB). */
    __asm volatile("cpsid i" ::: "memory");

    /* Relocate the vector table, then fence so the new table is committed and
     * the pipeline is flushed before we switch stacks and branch. */
    SCB_VTOR = vt;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    /* Switch MSP to the app's initial stack pointer and branch to reset handler. */
    __asm volatile(
        "msr msp, %0\n"
        "bx  %1\n"
        :
        : "r"(sp), "r"(pc)
        :);

    /* Unreachable — satisfies the noreturn attribute with no UB. */
    for (;;) {
    }
}
