/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef SEC_ECDSA_H
#define SEC_ECDSA_H

#include <stddef.h>
#include <stdint.h>

/**
 * Verify an ECDSA-P256 signature over a message payload.
 *
 * Computes SHA-256 over @p payload and verifies the raw 64-byte r||s signature
 * against the bootloader's baked secp256r1 public key (image_pubkey.h) using
 * mbedTLS (mbedtls_ecdsa_verify on the low-level r,s MPIs — no ASN.1/DER).
 *
 * @param payload  payload bytes (the same bytes the CRC-32 covers)
 * @param len      payload length in bytes
 * @param sig64    signature, raw r||s, 32 + 32 big-endian (64 bytes)
 * @return 1 if the signature is valid, 0 otherwise (mismatch or any error)
 */
int ecdsa_verify_p256(const uint8_t *payload, size_t len, const uint8_t sig64[64]);

#endif /* SEC_ECDSA_H */
