/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef SEC_RSA_H
#define SEC_RSA_H

#include <stddef.h>
#include <stdint.h>

/** RSA-2048 PKCS#1 v1.5 signature size in bytes (raw big-endian). */
#define SEC_RSA_SIG_SIZE 256u

/**
 * Verify an RSA-2048 PKCS#1 v1.5 signature over a message payload.
 *
 * Computes SHA-256 over @p payload and verifies the 256-byte PKCS#1 v1.5
 * signature against the bootloader's baked RSA-2048 public key
 * (image_pubkey.h: modulus n + exponent e = 65537) using mbedTLS
 * (mbedtls_rsa_pkcs1_verify with MBEDTLS_MD_SHA256).
 *
 * @param payload  payload bytes (the same bytes the CRC-32 covers)
 * @param len      payload length in bytes
 * @param sig      signature, 256 raw big-endian bytes (RSA-2048)
 * @return 0 if the signature is valid, non-zero otherwise (mismatch or any error)
 */
int rsa_verify_sha256(const uint8_t *payload, size_t len, const uint8_t sig[SEC_RSA_SIG_SIZE]);

#endif /* SEC_RSA_H */
