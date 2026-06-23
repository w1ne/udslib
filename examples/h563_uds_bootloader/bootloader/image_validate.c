/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "image_validate.h"
#include "ota_image.h"
#include "ota_crc.h"
#include "sec_rsa.h"

#include <string.h>

/* STM32H563 SRAM window: [RAM_BASE, RAM_END).
 * RAM_END (0x200A0000) is the first address one past the last valid RAM byte
 * (640 KB = 0xA0000). For a Cortex-M full-descending stack the conventional
 * initial SP equals RAM_END: it points one past the top of RAM and the first
 * push pre-decrements into valid RAM. RAM_END is therefore an ACCEPTED initial
 * SP value; only addresses below RAM_BASE or strictly above RAM_END are out of
 * range. (The genuine app links _estack = ORIGIN(RAM) + LENGTH(RAM) = RAM_END.)
 */
#define RAM_BASE 0x20000000UL
#define RAM_END 0x200A0000UL

int image_validate(const uint8_t *img, size_t avail)
{
    /* 0. Enough bytes to even read the header. */
    if (img == NULL || avail < OTA_IMAGE_HDR_SIZE) {
        return 0;
    }

    const ota_image_header_t *hdr = (const ota_image_header_t *) img;

    /* 1. Magic word. */
    if (hdr->magic != OTA_IMAGE_MAGIC) {
        return 0;
    }

    /* 2. Payload size plausibility. */
    if (hdr->image_size == 0u || hdr->image_size > OTA_IMAGE_MAX_PAYLOAD) {
        return 0;
    }

    /* 3. Header + payload + signature must all fit within the buffer/region.
     * Use size_t arithmetic; image_size is already bounded above so this cannot
     * overflow. */
    size_t needed =
        (size_t) OTA_IMAGE_HDR_SIZE + (size_t) hdr->image_size + (size_t) OTA_IMAGE_SIG_SIZE;
    if (needed > avail) {
        return 0;
    }

    /* 4. CRC-32 over the payload (fast integrity check — defense in depth). */
    const uint8_t *payload = img + OTA_IMAGE_HDR_SIZE;
    if (ota_crc32(payload, hdr->image_size) != hdr->crc32) {
        return 0;
    }

    /* 5. RSA-2048 authenticity: the 256-byte PKCS#1 v1.5 signature sits
     * immediately after the payload. Verify it over SHA-256(payload) against
     * the baked public key — only images signed with the matching private key
     * are accepted. A forged-but-CRC-good image fails here. */
    const uint8_t *sig = payload + hdr->image_size;
    if (rsa_verify_sha256(payload, hdr->image_size, sig) != 0) {
        return 0;
    }

    /* 6. Initial SP (first payload word = vector table[0]) must lie in RAM.
     * RAM_END is accepted (full-descending-stack top); see note above. */
    uint32_t initial_sp;
    memcpy(&initial_sp, payload, sizeof(initial_sp));
    if (initial_sp < RAM_BASE || initial_sp > RAM_END) {
        return 0;
    }

    return 1;
}
