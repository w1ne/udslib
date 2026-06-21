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
 *   [app_base+0x000 .. app_base+0x010)   ota_image_header_t (magic/image_size/crc32/version)
 *   [app_base+0x010 .. app_base+0x400)   RESERVED padding (filled with 0xFF by mkimage.py)
 *   [app_base+0x400 .. )                 app payload; the app's Cortex-M vector table starts
 *                                         at app_base + 0x400 = app_base + OTA_IMAGE_HDR_SIZE.
 *
 * The header region is 0x400 (1024) bytes so that the vector table lands on a
 * 1024-byte boundary.  Cortex-M33 VTOR[6:0] are RES0, so SCB->VTOR must be
 * aligned to the next power-of-two >= vector_table_size.  A full STM32H563
 * vector table (16 core + ~131 device IRQs = ~147 entries, 588 bytes) rounds up
 * to 1024 bytes, requiring 0x400 alignment.
 * app_base (= 0x08018000) is already 0x400-aligned, so
 *   app_base + OTA_IMAGE_HDR_SIZE = 0x08018400, and 0x08018400 & 0x3FF == 0. ✓
 *
 * All multi-byte fields are little-endian (native Cortex-M byte order).
 * The CRC covers only the payload bytes [app_base+0x400, app_base+0x400+image_size)
 * — it does NOT cover the header or padding.
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
    uint32_t image_size;  /**< Payload size in bytes (excludes the 0x400-byte header). */
    uint32_t crc32;       /**< CRC-32/ISO-HDLC over [app_base+0x400, app_base+0x400+image_size). */
    uint32_t version;     /**< App version (informational, e.g. 0x00010000 = v1.0.0). */
} ota_image_header_t;

/**
 * Total header region size in bytes (1024 / 0x400).
 * Only the first 16 bytes are the ota_image_header_t struct; bytes [16..0x400) are
 * RESERVED padding (0xFF).  The app's vector table starts at app_base + OTA_IMAGE_HDR_SIZE.
 */
#define OTA_IMAGE_HDR_SIZE  0x400u

/**
 * Maximum permitted payload size for the H563 dual-bank layout:
 *   app region = BANK_SIZE(1MB) - BL_REGION_SIZE(96KB) = 0xE8000 bytes
 *   minus the 0x400-byte header leaves 0xE7C00 bytes for the payload.
 */
#define OTA_IMAGE_MAX_PAYLOAD  (0xE8000u - OTA_IMAGE_HDR_SIZE)

#endif /* OTA_IMAGE_H */
