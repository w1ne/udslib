/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "app_jump.h"
#include "ota_image.h"
#include "ota_crc.h"
#include "sec_ecdsa.h"
#include <stdint.h>

/* STM32H563 SRAM: 0x20000000 .. 0x200A0000 (640 KB total) */
#define RAM_BASE  0x20000000UL
#define RAM_END   0x200A0000UL

/* SCB->VTOR register (Cortex-M33 System Control Block) */
#define SCB_VTOR  (*(volatile uint32_t *) 0xE000ED08UL)

int app_is_valid(uint32_t app_base)
{
    const ota_image_header_t *hdr = (const ota_image_header_t *) (uintptr_t) app_base;

    /* 1. Magic word */
    if (hdr->magic != OTA_IMAGE_MAGIC) {
        return 0;
    }

    /* 2. Payload size plausibility */
    if (hdr->image_size == 0u || hdr->image_size > OTA_IMAGE_MAX_PAYLOAD) {
        return 0;
    }

    /* 3. CRC-32 over the payload (fast integrity check — defense in depth). */
    const uint8_t *payload = (const uint8_t *) (uintptr_t) (app_base + OTA_IMAGE_HDR_SIZE);
    uint32_t computed = ota_crc32(payload, hdr->image_size);
    if (computed != hdr->crc32) {
        return 0;
    }

    /* 4. ECDSA-P256 authenticity: the 64-byte raw r||s signature sits
     * immediately after the payload. Verify it over SHA-256(payload) against
     * the baked public key — only images signed with the matching private key
     * are accepted. A forged-but-CRC-good image fails here. */
    const uint8_t *sig =
        (const uint8_t *) (uintptr_t) (app_base + OTA_IMAGE_HDR_SIZE + hdr->image_size);
    if (!ecdsa_verify_p256(payload, hdr->image_size, sig)) {
        return 0;
    }

    /* 5. Initial SP must be within RAM */
    uint32_t initial_sp = *((const uint32_t *) (uintptr_t) (app_base + OTA_IMAGE_HDR_SIZE));
    if (initial_sp < RAM_BASE || initial_sp > RAM_END) {
        return 0;
    }

    return 1;
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
        :
    );

    /* Unreachable — satisfies the noreturn attribute with no UB. */
    for (;;) {}
}
