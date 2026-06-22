/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file app_jump.h
 * @brief Boot-time app image validation and Cortex-M jump.
 *
 * Expected flash layout at app_base:
 *   [app_base .. app_base+16)   ota_image_header_t
 *   [app_base+16 .. )           app payload (Cortex-M vector table at app_base+16)
 */

#ifndef APP_JUMP_H
#define APP_JUMP_H

#include <stdint.h>

/**
 * Check whether a self-locating OTA image at @p app_base is valid.
 *
 * Thin wrapper that calls the shared validation core image_validate() (see
 * image_validate.h) over the bank's memory-mapped app region. The firmware and
 * the host unit test exercise the SAME core so they cannot diverge.
 *
 * Checks performed (in order):
 *  1. header.magic == OTA_IMAGE_MAGIC
 *  2. header.image_size > 0 && <= OTA_IMAGE_MAX_PAYLOAD
 *  3. header + payload + RSA signature all fit within the app region
 *  4. CRC-32/ISO-HDLC over the payload == header.crc32
 *  5. RSA-2048 PKCS#1 v1.5 signature over SHA-256(payload) verifies
 *  6. Initial SP at app_base+OTA_IMAGE_HDR_SIZE lies within RAM
 *     [0x20000000, 0x200A0000] (top-of-RAM 0x200A0000 is accepted: the
 *     conventional full-descending-stack initial SP points one past the last
 *     RAM byte)
 *
 * @param app_base  Flash address of the image header (= bank_base + 0x18000).
 * @return 1 if all checks pass, 0 otherwise.
 */
int app_is_valid(uint32_t app_base);

/**
 * Jump to the app image at @p app_base.
 *
 * Sets SCB->VTOR to app_base + 16, loads MSP from the first vector-table
 * entry, then branches (thumb) to the reset handler (second entry).
 * Interrupts are disabled before the jump. Does not return.
 *
 * @param app_base  Flash address of the image header (= bank_base + 0x18000).
 */
__attribute__((noreturn)) void app_jump(uint32_t app_base);

#endif /* APP_JUMP_H */
