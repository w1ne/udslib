/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/* RFC 4493 AES-CMAC test vectors */
#include "sec_cmac.h"
#include <stdio.h>
#include <string.h>

/* RFC 4493, Section 4, Test Case #1: AES-128-CMAC, Mlen=0 */
static const uint8_t rfc4493_key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
static const uint8_t rfc4493_mac0[16] = {0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
                                         0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46};
/* RFC 4493, Section 4, Test Case #2: Mlen=16 */
static const uint8_t rfc4493_msg1[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                                         0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
static const uint8_t rfc4493_mac1[16] = {0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
                                         0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c};

/*
 * Demo SecurityAccess credentials.  demo_secret matches bootloader/main.c
 * DEMO_SECRET.  The seed is now drawn per-attempt from the H563 TRNG, so this
 * fixed demo_seed is a deterministic AES-CMAC test vector only: it asserts that
 * aes_cmac(demo_secret, demo_seed, 16) equals the known-good value below,
 * proving the same one-shot the bootloader and tester run agrees with a
 * reference key for a known seed.
 */
static const uint8_t demo_secret[16] = {0xA3, 0xF1, 0x7C, 0x28, 0xB6, 0x4E, 0xD9, 0x05,
                                        0x71, 0xCC, 0x3A, 0x8F, 0x52, 0x0B, 0xE4, 0x96};
static const uint8_t demo_seed[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                                      0x91, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7, 0x08};
/* Known-good AES-128-CMAC(demo_secret, demo_seed). */
static const uint8_t demo_key[16] = {0x5F, 0xAC, 0xED, 0x58, 0x61, 0xBA, 0xC1, 0x37,
                                     0x66, 0x8A, 0xD5, 0x25, 0x4D, 0xED, 0xB2, 0x44};

static int run_demo_key_vector(void)
{
    uint8_t out[16];
    int rc = aes_cmac(demo_secret, demo_seed, sizeof(demo_seed), out);
    if (rc != 0) {
        printf("FAIL [demo-securityaccess-key]: aes_cmac returned %d\n", rc);
        return -1;
    }
    if (memcmp(out, demo_key, 16) != 0) {
        printf("FAIL [demo-securityaccess-key]: live key != known-good key\n");
        printf("  got:      ");
        for (int i = 0; i < 16; i++) printf("%02x", out[i]);
        printf("\n  expected: ");
        for (int i = 0; i < 16; i++) printf("%02x", demo_key[i]);
        printf("\n");
        return -1;
    }
    printf("PASS [demo-securityaccess-key]\n");
    return 0;
}

static int run_vector(const char *name, const uint8_t *msg, size_t mlen, const uint8_t *expected)
{
    uint8_t out[16];
    int rc = aes_cmac(rfc4493_key, msg, mlen, out);
    if (rc != 0) {
        printf("FAIL [%s]: aes_cmac returned %d\n", name, rc);
        return -1;
    }
    if (memcmp(out, expected, 16) != 0) {
        printf("FAIL [%s]: tag mismatch\n", name);
        printf("  got:      ");
        for (int i = 0; i < 16; i++) printf("%02x", out[i]);
        printf("\n  expected: ");
        for (int i = 0; i < 16; i++) printf("%02x", expected[i]);
        printf("\n");
        return -1;
    }
    printf("PASS [%s]\n", name);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= run_vector("RFC4493-v1-Mlen=0", NULL, 0, rfc4493_mac0);
    rc |= run_vector("RFC4493-v2-Mlen=16", rfc4493_msg1, 16, rfc4493_mac1);
    rc |= run_demo_key_vector();
    return rc;
}
