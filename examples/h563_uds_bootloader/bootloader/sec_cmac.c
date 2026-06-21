/* SPDX-License-Identifier: Apache-2.0 */
#include "sec_cmac.h"
#include "mbedtls/cipher.h"
#include "mbedtls/cmac.h"
#include "mbedtls/memory_buffer_alloc.h"

static uint8_t s_mbedtls_buf[4096];

void sec_crypto_init(void)
{
    mbedtls_memory_buffer_alloc_init(s_mbedtls_buf, sizeof(s_mbedtls_buf));
}

int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[16])
{
    /* mbedtls_cipher_cmac rejects NULL input even for zero-length messages */
    static const uint8_t s_dummy[1] = {0};
    const uint8_t *input = (msg_len == 0u) ? s_dummy : msg;

    const mbedtls_cipher_info_t *ci =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (ci == NULL) {
        return -1;
    }
    return mbedtls_cipher_cmac(ci, key, 128u, input, msg_len, out);
}
