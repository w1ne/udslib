/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file app_image_test.c
 * @brief Host unit test for OTA image header validation logic.
 *
 * Exercises the pure CRC/magic/size checks from app_is_valid() without any
 * MCU-specific code (no SCB, no MSP asm, no inline assembly).
 *
 * Build: gcc -O2 -g -I. -Wall -Wextra -Werror app_image_test.c ota_crc.c -o image_test
 */

#include "ota_image.h"
#include "ota_crc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----- portable validation logic (mirrors app_is_valid without MCU deps) -- */

#define RAM_BASE 0x20000000UL
#define RAM_END 0x200A0000UL

/**
 * Validate an OTA image stored in a host buffer.
 *
 * @param buf     Pointer to the image (header at buf[0]).
 * @param buf_len Total number of bytes in buf (must be >= OTA_IMAGE_HDR_SIZE).
 * @return 1 if valid, 0 otherwise.
 */
static int image_buf_is_valid(const uint8_t *buf, uint32_t buf_len)
{
    if (buf_len < OTA_IMAGE_HDR_SIZE) {
        return 0;
    }

    const ota_image_header_t *hdr = (const ota_image_header_t *) buf;

    /* 1. Magic */
    if (hdr->magic != OTA_IMAGE_MAGIC) {
        return 0;
    }

    /* 2. Size plausibility */
    if (hdr->image_size == 0u || hdr->image_size > OTA_IMAGE_MAX_PAYLOAD) {
        return 0;
    }
    if ((uint32_t) OTA_IMAGE_HDR_SIZE + hdr->image_size > buf_len) {
        return 0; /* buffer too small for claimed payload */
    }

    /* 3. CRC over payload */
    const uint8_t *payload = buf + OTA_IMAGE_HDR_SIZE;
    uint32_t computed = ota_crc32(payload, hdr->image_size);
    if (computed != hdr->crc32) {
        return 0;
    }

    /* 4. Initial SP plausibility (first word of payload = vector table[0]) */
    uint32_t initial_sp;
    memcpy(&initial_sp, payload, sizeof(initial_sp));
    if (initial_sp < RAM_BASE || initial_sp > RAM_END) {
        return 0;
    }

    return 1;
}

/* ----- helpers ----------------------------------------------------------- */

#define CHECK(cond, msg)                        \
    do {                                        \
        if (!(cond)) {                          \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                           \
        }                                       \
        printf("PASS: %s\n", msg);              \
    } while (0)

/* Build a valid [header][payload] blob into buf[].
 * payload_len must be <= sizeof(buf) - OTA_IMAGE_HDR_SIZE. */
static void build_image(uint8_t *buf, uint32_t payload_len, uint32_t initial_sp, uint32_t version)
{
    memset(buf, 0, OTA_IMAGE_HDR_SIZE + payload_len);

    /* Fill payload with a recognisable pattern; put initial_sp at offset 0. */
    uint8_t *payload = buf + OTA_IMAGE_HDR_SIZE;
    memcpy(payload, &initial_sp, sizeof(initial_sp));
    for (uint32_t i = sizeof(initial_sp); i < payload_len; i++) {
        payload[i] = (uint8_t) (i & 0xFFu);
    }

    uint32_t crc = ota_crc32(payload, payload_len);

    ota_image_header_t hdr;
    hdr.magic = OTA_IMAGE_MAGIC;
    hdr.image_size = payload_len;
    hdr.crc32 = crc;
    hdr.version = version;
    memcpy(buf, &hdr, sizeof(hdr));
}

/* ----- test cases -------------------------------------------------------- */

static int test_valid_image(void)
{
    /* 256-byte payload with a plausible initial SP */
    const uint32_t PAYLOAD_LEN = 256u;
    const uint32_t INITIAL_SP = 0x20010000u; /* within STM32H563 SRAM */
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 256];

    build_image(buf, PAYLOAD_LEN, INITIAL_SP, 0x00010000u);
    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 1, "valid image is accepted");
    return 0;
}

static int test_corrupt_payload_byte(void)
{
    const uint32_t PAYLOAD_LEN = 256u;
    const uint32_t INITIAL_SP = 0x20010000u;
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 256];

    build_image(buf, PAYLOAD_LEN, INITIAL_SP, 0x00010000u);

    /* Flip one byte in the middle of the payload */
    buf[OTA_IMAGE_HDR_SIZE + 128] ^= 0xFFu;

    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 0, "corrupted payload byte is rejected");
    return 0;
}

static int test_corrupt_magic(void)
{
    const uint32_t PAYLOAD_LEN = 64u;
    const uint32_t INITIAL_SP = 0x20020000u;
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 64];

    build_image(buf, PAYLOAD_LEN, INITIAL_SP, 0x00010001u);

    /* Overwrite the magic field */
    uint32_t bad_magic = 0xDEADBEEFu;
    memcpy(buf, &bad_magic, sizeof(bad_magic));

    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 0, "corrupted magic is rejected");
    return 0;
}

static int test_zero_image_size(void)
{
    const uint32_t PAYLOAD_LEN = 64u;
    const uint32_t INITIAL_SP = 0x20030000u;
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 64];

    build_image(buf, PAYLOAD_LEN, INITIAL_SP, 0x00010002u);

    /* Force image_size = 0 */
    ota_image_header_t *hdr = (ota_image_header_t *) buf;
    hdr->image_size = 0u;

    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 0, "zero image_size is rejected");
    return 0;
}

static int test_sp_out_of_ram(void)
{
    const uint32_t PAYLOAD_LEN = 64u;
    /* SP in flash — not RAM */
    const uint32_t BAD_SP = 0x08040000u;
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 64];

    build_image(buf, PAYLOAD_LEN, BAD_SP, 0x00010003u);

    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 0, "initial SP outside RAM is rejected");
    return 0;
}

static int test_corrupt_crc_field(void)
{
    const uint32_t PAYLOAD_LEN = 128u;
    const uint32_t INITIAL_SP = 0x20040000u;
    uint8_t buf[OTA_IMAGE_HDR_SIZE + 128];

    build_image(buf, PAYLOAD_LEN, INITIAL_SP, 0x00010004u);

    /* Corrupt the stored CRC (bytes 8-11 of the header) */
    buf[8] ^= 0x01u;

    CHECK(image_buf_is_valid(buf, sizeof(buf)) == 0, "corrupted crc32 field is rejected");
    return 0;
}

/* ----- entry point ------------------------------------------------------- */

int main(void)
{
    int rc = 0;
    rc |= test_valid_image();
    rc |= test_corrupt_payload_byte();
    rc |= test_corrupt_magic();
    rc |= test_zero_image_size();
    rc |= test_sp_out_of_ram();
    rc |= test_corrupt_crc_field();

    if (rc == 0) {
        printf("\nAll image-test cases PASS\n");
    }
    else {
        fprintf(stderr, "\nSome image-test cases FAILED\n");
    }
    return rc;
}
