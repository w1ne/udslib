/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "sec_ecdsa.h"
#include "image_pubkey.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/sha256.h"

int ecdsa_verify_p256(const uint8_t *payload, size_t len, const uint8_t sig64[64])
{
    int ret = 0;
    uint8_t hash[32];

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r;
    mbedtls_mpi s;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    /* SHA-256 over the payload (is224 = 0). */
    if (mbedtls_sha256(payload, len, hash, 0) != 0) {
        goto done;
    }

    /* Load the secp256r1 group parameters. */
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
        goto done;
    }

    /* Build the public key point from the baked X||Y in SEC1 uncompressed form
     * (0x04 || X || Y) and import it. */
    {
        uint8_t pt[1u + sizeof(IMAGE_PUBKEY_XY)];
        pt[0] = 0x04u; /* uncompressed point marker */
        for (size_t i = 0u; i < sizeof(IMAGE_PUBKEY_XY); i++) {
            pt[1u + i] = IMAGE_PUBKEY_XY[i];
        }
        if (mbedtls_ecp_point_read_binary(&grp, &Q, pt, sizeof(pt)) != 0) {
            goto done;
        }
    }

    /* Split the raw signature into r and s MPIs (big-endian, 32 bytes each). */
    if (mbedtls_mpi_read_binary(&r, &sig64[0], 32u) != 0) {
        goto done;
    }
    if (mbedtls_mpi_read_binary(&s, &sig64[32], 32u) != 0) {
        goto done;
    }

    /* Verify against the hash. Returns 0 only on a valid signature. */
    if (mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s) == 0) {
        ret = 1;
    }

done:
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ret;
}
