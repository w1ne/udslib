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
 * Checks performed (in order):
 *  1. header.magic == OTA_IMAGE_MAGIC
 *  2. header.image_size > 0 && <= OTA_IMAGE_MAX_PAYLOAD
 *  3. CRC-32/ISO-HDLC over [app_base+16, app_base+16+image_size) == header.crc32
 *  4. Initial SP at app_base+16 lies within RAM [0x20000000, 0x200A0000)
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
