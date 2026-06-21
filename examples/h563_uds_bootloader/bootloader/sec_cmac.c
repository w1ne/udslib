/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "sec_cmac.h"
#include "mbedtls/cipher.h"
#include "mbedtls/cmac.h"

int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[16])
{
    /* mbedtls_cipher_cmac rejects a NULL input pointer even for a zero-length
     * message, so hand it a valid (unread) pointer in that case. */
    static const uint8_t s_dummy[1] = {0};
    const uint8_t *input = (msg_len == 0u) ? s_dummy : msg;

    /* keylen is in BITS for the mbedTLS CMAC one-shot. */
    const mbedtls_cipher_info_t *ci =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (ci == NULL) {
        return -1;
    }
    return mbedtls_cipher_cmac(ci, key, 128u, input, msg_len, out);
}
