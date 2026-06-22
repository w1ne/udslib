/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/*
 * Minimal mbedTLS configuration for bare-metal AES-128-CMAC on Cortex-M33.
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
 * Memory: use the C library's calloc/free directly (newlib-nano on target,
 * glibc on the host test). mbedTLS calls calloc/free unless told otherwise, so
 * no MBEDTLS_PLATFORM_* / MBEDTLS_MEMORY_BUFFER_ALLOC_C is needed. _sbrk in
 * syscalls.c backs the heap on target.
 */

/* No entropy/net/fs/time/threading/self-test/PSA */
#define MBEDTLS_NO_PLATFORM_ENTROPY

#endif /* MBEDTLS_BOOTLOADER_CONFIG_H */
