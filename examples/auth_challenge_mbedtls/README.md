# Authentication (0x29) with real AES-CMAC (mbedTLS or wolfSSL)

The production-shaped sibling of [`../auth_challenge`](../auth_challenge). That
example uses a non-cryptographic placeholder tag so it builds with zero
dependencies; **this one verifies the proof of ownership with real
AES-128-CMAC**, plugged into the same `fn_auth` hook.

The same example builds against **two** crypto libraries — `make` (mbedTLS,
default) or `make CRYPTO=wolfssl` — and both produce **byte-identical** output
through the full flow. Only `aes_cmac()` and the linked library differ; udslib
itself is untouched. That is the point: the hook contract is bytes-in /
bytes-out, so the back-end is interchangeable.

## What changes vs. the placeholder example — and what does not

Only the application's crypto changes. udslib is identical: it still owns the
0x29 plumbing (sub-function validation, the `ctx.security.authenticated` state machine,
NRC framing, service gating via `fn_auth_required`) and still bundles **no
cipher**. The library is never recompiled or relinked against mbedTLS.

The crypto lives entirely in one helper:

```c
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len,
                    uint8_t out[16])
{
    const mbedtls_cipher_info_t *ci =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    return mbedtls_cipher_cmac(ci, key, 128u, msg, msg_len, out); /* keylen in bits */
}
```

That is the only call site. The **wolfSSL** back-end (selected by
`-DUSE_WOLFSSL`) is the same helper with one different body:

```c
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len,
                    uint8_t out[16])
{
    word32 out_len = 16;
    return wc_AesCmacGenerate(out, &out_len, msg, (word32) msg_len, key, 16);
}
```

To move to an **HSM/SHE**, replace it with your key-handle CMAC call —
`k_aes_key` becomes a handle the CPU can't read, which is exactly why the crypto
has to be a hook.

> **Caveat — "interchangeable" is not "zero-config".** The *code* swap is one
> function, but the library must actually ship the primitive **and be built with
> it**: wolfSSL needs `WOLFSSL_CMAC` + `WOLFSSL_AES_DIRECT` compiled in (the
> Debian/Ubuntu `libwolfssl-dev` package has them; a trimmed embedded build may
> not). And both ends must agree on the *scheme* — if your tester uses
> HMAC-SHA256 or a certificate chain instead of AES-CMAC, you implement that
> scheme in the hook. The udslib seam works regardless; the crypto choice is
> yours to match.

## Why the library still doesn't bundle the cipher

- On a real ECU the key lives in an SHE/HSM the software cannot read, so a
  callback is the only integration that works — a bundled software AES couldn't
  even reach the key.
- A vetted crypto library (mbedTLS, wolfSSL, tinycrypt) or the HSM should own
  the primitives, not a protocol stack; bundling one enlarges the attack
  surface and the SBOM/CRA burden.
- License: udslib is PolyForm-Noncommercial; pulling third-party crypto into
  the core would entangle that. Linking it in *your* application (as here) does
  not.

The Security Access (0x27) `fn_security_seed` / `fn_security_key` hooks (see
[`../security_access_mbedtls`](../security_access_mbedtls)) and the
SecuredDataTransmission (0x84) `fn_secure_decode` / `fn_secure_encode` hooks
follow the identical pattern.

## The flow

```
(gated service 0xBA before auth)              -> NRC 0x34 authenticationRequired
verifyCertificateUnidirectional   (0x29 0x01) -> evaluation status
requestChallengeForAuthentication (0x29 0x05) -> server returns a 16-byte challenge
proofOfOwnership                  (0x29 0x03) -> client returns AES-CMAC_key(challenge),
                                                 server recomputes & compares (constant
                                                 time) -> ctx.security.authenticated
proofOfOwnership with a wrong tag             -> NRC 0x34, ctx.security.authenticated cleared
(gated service 0xBA after re-auth)            -> allowed
```

`deAuthenticate` (0x29 0x00) and `authenticationConfiguration` (0x08) are handled
natively. The library auto-clears `ctx.security.authenticated` on deAuthenticate, session
change, S3 timeout, and reset.

## Real crypto, two honest shortcuts

The AES-CMAC verification is real. Two things are fixed only to keep the output
deterministic, and both are marked in `main.c`:

- **The challenge nonce** is a fixed 16 bytes. In production fill it from your
  TRNG / HSM, e.g. `mbedtls_ctr_drbg_random(&drbg, g_challenge, 16)`.
- **The AES key** is the RFC 4493 test key in flash. In production it is a key
  handle inside the HSM.

## Build & run

Default (mbedTLS):

```sh
sudo apt-get install libmbedtls-dev   # Debian/Ubuntu
make run
```

Or wolfSSL — same example, same output:

```sh
sudo apt-get install libwolfssl-dev   # Debian/Ubuntu (built with WOLFSSL_CMAC)
make CRYPTO=wolfssl run
```

Expected output (identical for both back-ends):

```
=== 1. Gated service 0xBA BEFORE auth -> NRC 0x34 (authenticationRequired) ===
-> request: BA
  <- response: 7F BA 34

=== 2. verifyCertificateUnidirectional (0x29 0x01) ===
-> request: 29 01 C0 DE
  <- response: 69 01 01

=== 3. requestChallengeForAuthentication (0x29 0x05) ===
-> request: 29 05
  <- response: 69 05 11 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F

=== 4. proofOfOwnership with real AES-128-CMAC(challenge) -> verified ===
-> request: 29 03 42 9D 47 49 3E 8B 9C FF 84 1E 73 5D E0 07 71 FC
  <- response: 69 03 02
   ctx.security.authenticated = true

=== 5. proofOfOwnership with a WRONG tag -> NRC 0x34 ===
-> request: 29 05
  <- response: 69 05 11 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
-> request: 29 03 BD 9D 47 49 3E 8B 9C FF 84 1E 73 5D E0 07 71 FC
  <- response: 7F 29 34
   ctx.security.authenticated = false

=== 6. Re-authenticate, then gated service 0xBA -> allowed ===
-> request: 29 05
  <- response: 69 05 11 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
-> request: 29 03 42 9D 47 49 3E 8B 9C FF 84 1E 73 5D E0 07 71 FC
  <- response: 69 03 02
-> request: BA
  <- response: FA AC
```
