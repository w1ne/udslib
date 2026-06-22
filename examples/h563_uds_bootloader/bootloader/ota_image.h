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
 *   [app_base+0x000 .. app_base+0x010)              ota_image_header_t (magic/image_size/crc32/version)
 *   [app_base+0x010 .. app_base+0x400)              RESERVED padding (filled with 0xFF by mkimage.py)
 *   [app_base+0x400 .. +0x400+image_size)           app payload; the app's Cortex-M vector
 *                                                    table starts at app_base + 0x400 = app_base
 *                                                    + OTA_IMAGE_HDR_SIZE.
 *   [app_base+0x400+image_size .. +image_size+0x100) RSA-2048 PKCS#1 v1.5 signature (256 bytes)
 *
 * The signature offset is computed from image_size — no new header field is
 * needed. mkimage.py appends it; the bootloader reads it back from the same
 * offset for verification.
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
 * — it does NOT cover the header or padding.  The RSA-2048 PKCS#1 v1.5
 * signature covers SHA-256 of those exact same payload bytes (defense in depth:
 * CRC = fast integrity check, signature = authenticity).  See sec_rsa.h.
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
 * RSA-2048 PKCS#1 v1.5 signature size in bytes (256 raw big-endian bytes).
 * Appended immediately after the payload. The signature lives at
 *   app_base + OTA_IMAGE_HDR_SIZE + image_size .. + OTA_IMAGE_SIG_SIZE
 */
#define OTA_IMAGE_SIG_SIZE  256u

/**
 * Maximum permitted payload size for the H563 dual-bank layout:
 *   app region = BANK_SIZE(1MB) - BL_REGION_SIZE(96KB) = 0xE8000 bytes
 *   minus the 0x400-byte header leaves 0xE7C00 bytes for the payload.
 */
#define OTA_IMAGE_MAX_PAYLOAD  (0xE8000u - OTA_IMAGE_HDR_SIZE)

/* ---------------------------------------------------------------------------
 * Anti-rollback policy (configurable)
 *
 * Monotonic version enforcement: an OTA candidate whose header.version is LESS
 * than the currently-active app's header.version is a DOWNGRADE and is rejected,
 * preventing a forced revert to an older (possibly vulnerable) image.  Equal and
 * higher versions are always allowed.
 *
 * Compile-time switch OTA_ANTIROLLBACK_ENFORCE:
 *   1 (default) — enforce: downgrades rejected.
 *   0           — allow downgrades (override with -DOTA_ANTIROLLBACK_ENFORCE=0
 *                 at compile time, e.g. for field-recovery / lab reflash).
 *
 * Storage of the "minimum acceptable version": this example derives it from the
 * currently-active app's own header (read live at enforcement time) rather than
 * a dedicated monotonic counter sector.  Limitation: if the active bank holds no
 * valid image (recovery mode) there is no current version to compare against, so
 * enforcement is skipped and the candidate is allowed — recovery must never be
 * bricked by anti-rollback.  A production design would back the minimum version
 * with a one-way counter in protected flash / OTP so it cannot regress even when
 * both banks are reflashed.
 * ------------------------------------------------------------------------- */
#ifndef OTA_ANTIROLLBACK_ENFORCE
#define OTA_ANTIROLLBACK_ENFORCE 1
#endif

/**
 * ota_version_allows() — pure anti-rollback decision (no flash I/O, host-testable).
 *
 * @param candidate  Version of the image being activated.
 * @param current    Version currently installed/active (the floor).
 * @param enforce    Non-zero to enforce monotonicity (reject downgrades);
 *                   zero to allow any version.
 * @return 1 if activation is permitted, 0 if it must be rejected as a rollback.
 *
 * When enforce==0, always returns 1.  When enforce!=0, returns 1 iff
 * candidate >= current (upgrade or reinstall), 0 for a strict downgrade.
 */
static inline int ota_version_allows(uint32_t candidate, uint32_t current, int enforce)
{
    if (enforce == 0) {
        return 1; /* policy disabled: any version accepted */
    }
    return (candidate >= current) ? 1 : 0;
}

#endif /* OTA_IMAGE_H */
