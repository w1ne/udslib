/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/*
 * Minimal mbedTLS configuration for bare-metal AES-128-CMAC plus RSA-2048
 * PKCS#1 v1.5 image verification (secure boot) on Cortex-M33.
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
 * Image authenticity (secure boot): RSA-2048 PKCS#1 v1.5 signature
 * verification over SHA-256 of the app payload. RSA verify is m^e mod n with
 * e = 65537, ~100x cheaper than an ECDSA-P256 verify — it completes well under
 * the simulator's 50M-instruction cap, which the ECDSA path exceeded.
 *   MBEDTLS_RSA_C      — RSA verify (mbedtls_rsa_pkcs1_verify)
 *   MBEDTLS_PKCS1_V15  — PKCS#1 v1.5 signature scheme (RSASSA-PKCS1-v1_5)
 *   MBEDTLS_BIGNUM_C   — big-integer modular exponentiation (RSA_C prerequisite)
 *   MBEDTLS_OID_C      — SHA-256 OID for the PKCS#1 v1.5 DigestInfo (RSA_C prereq)
 *   MBEDTLS_MD_C       — message-digest dispatch used by the PKCS#1 v1.5 path
 *   MBEDTLS_SHA256_C / MBEDTLS_SHA224_C — payload digest (SHA-224 sibling pulled
 *                                         in by the shared sha256.c module)
 *   MBEDTLS_ASN1_PARSE_C/WRITE_C — pulled in transitively by rsa.c (key-parse +
 *                                  DigestInfo helpers reference mbedtls_asn1_*).
 */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_OID_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA224_C
/* asn1parse/asn1write: rsa.c's key-parse and DigestInfo helpers reference the
 * mbedtls_asn1_* symbols; these enable those modules so they resolve at link.
 * The verify path itself imports the public key as raw n/e (no DER). */
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
