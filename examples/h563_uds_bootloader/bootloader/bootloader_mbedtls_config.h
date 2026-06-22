/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/*
 * Minimal mbedTLS configuration for bare-metal AES-128-CMAC plus ECDSA-P256
 * image verification (secure boot) on Cortex-M33.
 * Included via -DMBEDTLS_CONFIG_FILE='"bootloader_mbedtls_config.h"'.
 *
 * NOTE: named bootloader_mbedtls_config.h (not mbedtls_config.h) to avoid
 * conflicting with the library's own include/mbedtls/mbedtls_config.h, which
 * would be found first when resolving a bare "mbedtls_config.h" include from
 * within the mbedtls source tree.
 */
#ifndef MBEDTLS_BOOTLOADER_CONFIG_H
#define MBEDTLS_BOOTLOADER_CONFIG_H

/* Crypto primitives needed for AES-CMAC */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CMAC_C
#define MBEDTLS_AES_ROM_TABLES  /* saves ~10KB RAM, uses ROM for S-boxes */

/*
 * Image authenticity (secure boot): ECDSA-P256 signature verification over
 * SHA-256 of the app payload.
 *   MBEDTLS_BIGNUM_C / MBEDTLS_ECP_C   — big-int + elliptic-curve arithmetic
 *   MBEDTLS_ECDSA_C                    — ECDSA verify (mbedtls_ecdsa_verify)
 *   MBEDTLS_ECP_DP_SECP256R1_ENABLED   — only NIST P-256 (keeps ROM small)
 *   MBEDTLS_SHA256_C / MBEDTLS_SHA224_C— payload digest (SHA-224 sibling pulled
 *                                        in by the shared sha256.c module)
 *   MBEDTLS_ASN1_PARSE_C/WRITE_C       — prerequisites the ECDSA_C config check
 *                                        requires (ecdsa.c references asn1write);
 *                                        the verify path itself uses raw r,s MPIs,
 *                                        not DER.
 */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

/*
 * Memory: use the C library's calloc/free directly (newlib-nano on target,
 * glibc on the host test). mbedTLS calls calloc/free unless told otherwise, so
 * no MBEDTLS_PLATFORM_* / MBEDTLS_MEMORY_BUFFER_ALLOC_C is needed. _sbrk in
 * syscalls.c backs the heap on target.
 */

/* No entropy/net/fs/time/threading/self-test/PSA */
#define MBEDTLS_NO_PLATFORM_ENTROPY

#endif /* MBEDTLS_BOOTLOADER_CONFIG_H */
