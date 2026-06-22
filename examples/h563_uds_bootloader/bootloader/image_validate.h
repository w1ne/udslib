/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file image_validate.h
 * @brief Pure OTA image validation core (no MCU-specific code).
 *
 * This is the single source of truth for "is this OTA image acceptable?".
 * It contains NO inline assembly, no SCB/MSP access and no flash I/O, so it
 * links and runs identically on the Cortex-M target and on the host test
 * machine. The firmware's app_is_valid() (app_jump.c) is a thin wrapper around
 * image_validate(); the host unit test (app_image_test.c) calls the SAME
 * function on real signed image buffers. The two can never silently diverge.
 *
 * Expected buffer layout (matches ota_image.h):
 *   [0 .. OTA_IMAGE_HDR_SIZE)                          ota_image_header_t + padding
 *   [OTA_IMAGE_HDR_SIZE .. +image_size)               payload (vector table at offset 0)
 *   [OTA_IMAGE_HDR_SIZE+image_size .. +OTA_IMAGE_SIG_SIZE) RSA-2048 signature
 */

#ifndef IMAGE_VALIDATE_H
#define IMAGE_VALIDATE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Validate an OTA image held in a contiguous buffer.
 *
 * Checks performed (in order):
 *  1. @p avail is large enough to hold the header.
 *  2. header.magic == OTA_IMAGE_MAGIC.
 *  3. header.image_size in (0, OTA_IMAGE_MAX_PAYLOAD].
 *  4. header + payload + signature all fit within @p avail.
 *  5. CRC-32/ISO-HDLC over the payload == header.crc32.
 *  6. RSA-2048 PKCS#1 v1.5 signature over SHA-256(payload) verifies against the
 *     baked public key (image_pubkey.h).
 *  7. Initial SP (first payload word) lies within RAM [RAM_BASE, RAM_END).
 *
 * @param img    Pointer to the image (header at img[0]).
 * @param avail  Number of bytes addressable from @p img (region/buffer size).
 * @return 1 if every check passes, 0 otherwise.
 */
int image_validate(const uint8_t *img, size_t avail);

#endif /* IMAGE_VALIDATE_H */
