/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file app_image_test.c
 * @brief Host unit test for OTA image validation.
 *
 * Exercises the REAL bootloader validation core image_validate() (see
 * image_validate.h) — the same function app_is_valid() wraps on the target —
 * against real signed image buffers loaded from disk. There is no forked copy
 * of the validation logic, so the firmware and this test cannot diverge.
 *
 * The full check chain is covered, including the RSA-2048 signature path:
 *   - the genuine signed App-B image passes;
 *   - a one-byte payload tamper (breaks CRC + signature) is rejected;
 *   - a bad magic is rejected;
 *   - a zero image_size is rejected;
 *   - a corrupted CRC field is rejected;
 *   - an initial SP outside RAM is rejected;
 *   - the CRC-good-but-signature-bad image (app_bsigbad_image.bin) is rejected,
 *     proving the signature gate (not the CRC) catches it;
 *   - a buffer too small for the claimed payload is rejected.
 *
 * Built and run on the host by the bootloader Makefile `image-test` target,
 * which links sec_rsa.c + the mbedTLS RSA/SHA-256 modules (as rsa-test does) so
 * the signature path is genuinely executed.
 */

#include "image_validate.h"
#include "ota_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Path is passed by the Makefile via -DIMG_DIR so the test is run-dir agnostic. */
#ifndef IMG_DIR
#define IMG_DIR "../app"
#endif

/* ----- helpers ----------------------------------------------------------- */

#define CHECK(cond, msg)                        \
    do {                                        \
        if (!(cond)) {                          \
            fprintf(stderr, "FAIL: %s\n", msg); \
            g_rc = 1;                           \
        }                                       \
        else {                                  \
            printf("PASS: %s\n", msg);          \
        }                                       \
    } while (0)

static int g_rc = 0;

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *buf = malloc((size_t) sz);
    if (!buf || fread(buf, 1, (size_t) sz, f) != (size_t) sz) {
        fprintf(stderr, "read error %s\n", path);
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t) sz;
    return buf;
}

/* Return a heap copy of the genuine signed image, or NULL on failure.
 * Caller frees. The header is also returned via *hdr for offset math. */
static uint8_t *load_genuine(size_t *len, ota_image_header_t *hdr)
{
    uint8_t *img = read_file(IMG_DIR "/app_b_image.bin", len);
    if (!img) {
        return NULL;
    }
    if (*len < OTA_IMAGE_HDR_SIZE + OTA_IMAGE_SIG_SIZE) {
        fprintf(stderr, "genuine image too short\n");
        free(img);
        return NULL;
    }
    memcpy(hdr, img, sizeof(*hdr));
    return img;
}

/* ----- entry point ------------------------------------------------------- */

int main(void)
{
    size_t len = 0;
    ota_image_header_t hdr;

    /* Genuine signed image: must pass every check (magic, size, CRC, RSA, SP). */
    uint8_t *img = load_genuine(&len, &hdr);
    if (!img) {
        return 1;
    }
    CHECK(image_validate(img, len) == 1, "genuine signed image is accepted");

    /* One-byte payload tamper: breaks CRC and signature → rejected. */
    {
        uint8_t *t = malloc(len);
        memcpy(t, img, len);
        t[OTA_IMAGE_HDR_SIZE + 64] ^= 0xFFu;
        CHECK(image_validate(t, len) == 0, "one-byte payload tamper is rejected");
        free(t);
    }

    /* Bad magic → rejected. */
    {
        uint8_t *t = malloc(len);
        memcpy(t, img, len);
        uint32_t bad_magic = 0xDEADBEEFu;
        memcpy(t, &bad_magic, sizeof(bad_magic));
        CHECK(image_validate(t, len) == 0, "corrupted magic is rejected");
        free(t);
    }

    /* Zero image_size → rejected. */
    {
        uint8_t *t = malloc(len);
        memcpy(t, img, len);
        ota_image_header_t *th = (ota_image_header_t *) t;
        th->image_size = 0u;
        CHECK(image_validate(t, len) == 0, "zero image_size is rejected");
        free(t);
    }

    /* Corrupted CRC field (bytes 8..11 of header) → rejected. */
    {
        uint8_t *t = malloc(len);
        memcpy(t, img, len);
        t[8] ^= 0x01u;
        CHECK(image_validate(t, len) == 0, "corrupted crc32 field is rejected");
        free(t);
    }

    /* Initial SP outside RAM (first payload word set to a flash address) →
     * rejected. CRC and signature would mismatch too, but the point is the
     * SP-in-RAM gate fires; we craft the case by pointing SP into flash. */
    {
        uint8_t *t = malloc(len);
        memcpy(t, img, len);
        uint32_t bad_sp = 0x08040000u; /* flash, not RAM */
        memcpy(t + OTA_IMAGE_HDR_SIZE, &bad_sp, sizeof(bad_sp));
        CHECK(image_validate(t, len) == 0, "initial SP outside RAM is rejected");
        free(t);
    }

    /* Buffer too small for the claimed header+payload+signature → rejected. */
    CHECK(image_validate(img, OTA_IMAGE_HDR_SIZE) == 0,
          "buffer too small for claimed payload is rejected");
    CHECK(image_validate(img, len - 1u) == 0, "buffer one byte short of signature end is rejected");

    free(img);

    /* CRC-good but signature-tampered image: only the RSA gate can reject it. */
    {
        size_t blen = 0;
        uint8_t *bad = read_file(IMG_DIR "/app_bsigbad_image.bin", &blen);
        if (!bad) {
            fprintf(stderr, "FAIL: could not read app_bsigbad_image.bin\n");
            g_rc = 1;
        }
        else {
            CHECK(image_validate(bad, blen) == 0,
                  "CRC-good signature-bad image is rejected (signature gate)");
            free(bad);
        }
    }

    if (g_rc == 0) {
        printf("\nAll image-test cases PASS\n");
    }
    else {
        fprintf(stderr, "\nSome image-test cases FAILED\n");
    }
    return g_rc;
}
