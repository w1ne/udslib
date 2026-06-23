/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "sec_rsa.h"
#include "image_pubkey.h"

#include "mbedtls/rsa.h"
#include "mbedtls/bignum.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

int rsa_verify_sha256(const uint8_t *payload, size_t len, const uint8_t sig[SEC_RSA_SIG_SIZE])
{
    int ret = -1;
    uint8_t hash[32];

    static const uint8_t exp_be[3] = {0x01u, 0x00u, 0x01u}; /* 65537, big-endian */

    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa);

    /* SHA-256 over the payload (is224 = 0). */
    if (mbedtls_sha256(payload, len, hash, 0) != 0) {
        goto done;
    }

    /* Import the baked public key (modulus n + exponent e); no private params. */
    if (mbedtls_rsa_import_raw(&rsa, IMAGE_PUBKEY_N, sizeof(IMAGE_PUBKEY_N), NULL, 0, NULL, 0, NULL,
                               0, exp_be, sizeof(exp_be)) != 0) {
        goto done;
    }
    if (mbedtls_rsa_complete(&rsa) != 0) {
        goto done;
    }

    /* PKCS#1 v1.5 verify over SHA-256(payload). Returns 0 only on success. */
    if (mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, (unsigned int) sizeof(hash), hash, sig) ==
        0) {
        ret = 0;
    }

done:
    mbedtls_rsa_free(&rsa);
    return ret;
}
