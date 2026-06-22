/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/**
 * @file image_pubkey.h
 * @brief DEMO secp256r1 (NIST P-256) public key baked into the bootloader.
 *
 * The bootloader verifies the OTA image signature against THIS key. Only an
 * image signed with the matching private key (app/signing_key_dev.pem) boots.
 *
 * Format: uncompressed affine point, X || Y, 64 bytes, big-endian. This is the
 * public key matching app/signing_key_dev.pem. It is auto-derived from that PEM
 * (see app/Makefile pubkey target) — do not hand-edit.
 *
 * SECURITY NOTE: this is a DEMO key committed for the example only. A real
 * product NEVER commits the signing private key; it lives in an HSM / offline
 * signing service. Rotating to product keys is a build-time swap of this file
 * (public half) and the PEM (private half).
 */
#ifndef IMAGE_PUBKEY_H
#define IMAGE_PUBKEY_H

#include <stdint.h>

/* secp256r1 public key, uncompressed X||Y (64 bytes, big-endian). */
static const uint8_t IMAGE_PUBKEY_XY[64] = {
    0x4a, 0x5b, 0x61, 0xb8, 0x59, 0xef, 0x96, 0xdd, 0x37, 0x0e, 0x30, 0x40, 0xad, 0x1d, 0x25, 0xfe,
    0x54, 0x7f, 0x71, 0xc4, 0x68, 0x1f, 0x9f, 0xc9, 0x60, 0x35, 0x52, 0xc1, 0xd7, 0x0e, 0x59, 0x61,
    0xc8, 0xdf, 0xbe, 0x76, 0x2b, 0xac, 0x66, 0xf6, 0x01, 0x65, 0xfb, 0x45, 0xf9, 0x2d, 0xce, 0x8f,
    0x12, 0x25, 0xb7, 0x89, 0x3c, 0x27, 0xf2, 0x32, 0x2d, 0xfb, 0x15, 0x27, 0x69, 0x8e, 0x65, 0x7c,
};

#endif /* IMAGE_PUBKEY_H */
