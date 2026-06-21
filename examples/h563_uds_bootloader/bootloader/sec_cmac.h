/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEC_CMAC_H
#define SEC_CMAC_H

#include <stddef.h>
#include <stdint.h>

/**
 * AES-128-CMAC one-shot.
 * @param key     16-byte AES key
 * @param msg     message to authenticate (may be NULL if msg_len == 0)
 * @param msg_len message length in bytes
 * @param out     16-byte output tag
 * @return 0 on success, non-zero on error
 */
int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t out[16]);

#endif /* SEC_CMAC_H */
