/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file ota_image.h
 * @brief Self-locating OTA image header format.
 *
 * Layout in flash for a bank's app slot (app_base = bank_base + 0x18000):
 *
 *   [app_base .. app_base+16)   ota_image_header_t  (this header)
 *   [app_base+16 .. )           app payload; the app's Cortex-M vector table
 *                                starts at app_base + OTA_IMAGE_HDR_SIZE.
 *
 * All multi-byte fields are little-endian (native Cortex-M byte order).
 * The CRC covers only the payload bytes — it does NOT cover the header itself.
 *
 * Algorithm: CRC-32/ISO-HDLC
 *   Poly:      0x04C11DB7 (reflected: 0xEDB88320)
 *   Init/XOR:  0xFFFFFFFF
 *   Reflect:   input + output
 * This is the same algorithm sometimes called "standard CRC-32" or "pkzip CRC-32".
 */

#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H

#include <stdint.h>

#define OTA_IMAGE_MAGIC  0xC0DEBEEFu

typedef struct {
    uint32_t magic;       /**< Must equal OTA_IMAGE_MAGIC. */
    uint32_t image_size;  /**< Payload size in bytes (excludes the 16-byte header). */
    uint32_t crc32;       /**< CRC-32/ISO-HDLC over [app_base+16, app_base+16+image_size). */
    uint32_t version;     /**< App version (informational, e.g. 0x00010000 = v1.0.0). */
} ota_image_header_t;

/** Size of the header in bytes. Always 16. */
#define OTA_IMAGE_HDR_SIZE  16u

/**
 * Maximum permitted payload size for the H563 dual-bank layout:
 *   app region = BANK_SIZE(1MB) - BL_REGION_SIZE(96KB) = 0xE8000 bytes
 *   minus the 16-byte header leaves 0xE7FF0 bytes for the payload.
 */
#define OTA_IMAGE_MAX_PAYLOAD  (0xE8000u - OTA_IMAGE_HDR_SIZE)

#endif /* OTA_IMAGE_H */
