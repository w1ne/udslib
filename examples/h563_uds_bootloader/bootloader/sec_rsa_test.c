/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/*
 * Host test for RSA-2048 image authenticity (secure boot).
 *
 * Proves, independent of the simulator:
 *   1. The committed signed image (app/app_b_image.bin) verifies against the
 *      baked public key (image_pubkey.h) via the same rsa_verify_sha256() the
 *      bootloader runs.
 *   2. A one-byte tamper of the payload makes verification FAIL.
 *   3. A signature-tampered image (app/app_bsigbad_image.bin) FAILS while its
 *      CRC still matches — proving the signature gate, not the CRC, rejects it.
 *
 * Built and run on the host (gcc) by the bootloader Makefile `rsa-test`
 * target, which compiles sec_rsa.c + the mbedTLS RSA/SHA-256 modules.
 */
#include "sec_rsa.h"
#include "ota_image.h"
#include "ota_crc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Path is passed by the Makefile via -DIMG_DIR so the test is run-dir agnostic. */
#ifndef IMG_DIR
#define IMG_DIR "../app"
#endif

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

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    int rc = 0;

    /* ---- Case 1+2: genuine signed App-B image -------------------------- */
    size_t len = 0;
    uint8_t *img = read_file(IMG_DIR "/app_b_image.bin", &len);
    if (!img) {
        return fail("could not read app_b_image.bin");
    }
    if (len < OTA_IMAGE_HDR_SIZE + OTA_IMAGE_SIG_SIZE) {
        free(img);
        return fail("image too short");
    }

    ota_image_header_t hdr;
    memcpy(&hdr, img, sizeof(hdr));
    if (hdr.magic != OTA_IMAGE_MAGIC) {
        free(img);
        return fail("bad magic");
    }

    const uint8_t *payload = img + OTA_IMAGE_HDR_SIZE;
    const uint8_t *sig = payload + hdr.image_size;
    if ((size_t) (OTA_IMAGE_HDR_SIZE + hdr.image_size + OTA_IMAGE_SIG_SIZE) != len) {
        free(img);
        return fail("size mismatch (header+payload+sig != file)");
    }

    /* CRC must match (defense-in-depth fast check). */
    if (ota_crc32(payload, hdr.image_size) != hdr.crc32) {
        free(img);
        return fail("genuine image CRC mismatch");
    }
    printf("PASS: genuine image CRC ok (0x%08X)\n", hdr.crc32);

    /* Signature must verify (rsa_verify_sha256 returns 0 on success). */
    if (rsa_verify_sha256(payload, hdr.image_size, sig) != 0) {
        free(img);
        return fail("genuine signature did NOT verify");
    }
    printf("PASS: genuine RSA-2048 signature verifies\n");

    /* Tamper one payload byte → signature must now fail. */
    uint8_t *tampered = malloc(len);
    memcpy(tampered, img, len);
    uint8_t *t_payload = tampered + OTA_IMAGE_HDR_SIZE;
    t_payload[0] ^= 0x01u;
    if (rsa_verify_sha256(t_payload, hdr.image_size,
                          tampered + OTA_IMAGE_HDR_SIZE + hdr.image_size) == 0) {
        free(img);
        free(tampered);
        return fail("payload tamper was ACCEPTED (should be rejected)");
    }
    printf("PASS: one-byte payload tamper rejected\n");
    free(tampered);
    free(img);

    /* ---- Case 3: CRC-good but signature-tampered image ----------------- */
    size_t blen = 0;
    uint8_t *bad = read_file(IMG_DIR "/app_bsigbad_image.bin", &blen);
    if (!bad) {
        return fail("could not read app_bsigbad_image.bin");
    }
    ota_image_header_t bhdr;
    memcpy(&bhdr, bad, sizeof(bhdr));
    const uint8_t *b_payload = bad + OTA_IMAGE_HDR_SIZE;
    const uint8_t *b_sig = b_payload + bhdr.image_size;

    if (ota_crc32(b_payload, bhdr.image_size) != bhdr.crc32) {
        free(bad);
        return fail("sig-bad image CRC unexpectedly mismatched");
    }
    printf("PASS: sig-bad image CRC is still good (0x%08X)\n", bhdr.crc32);

    if (rsa_verify_sha256(b_payload, bhdr.image_size, b_sig) == 0) {
        free(bad);
        return fail("tampered signature was ACCEPTED (should be rejected)");
    }
    printf("PASS: tampered signature rejected (signature gate works)\n");
    free(bad);

    printf("ALL RSA SECURE-BOOT TESTS PASSED\n");
    return rc;
}
