# Security Access (0x27) with real AES-CMAC (mbedTLS or wolfSSL)

The production-shaped sibling of [`../pro_flash_tool`](../pro_flash_tool), whose
seed→key step is a non-cryptographic `seed ^ 0xFF` placeholder so it builds with
zero dependencies. **This example derives the key from the seed with real
AES-128-CMAC**, plugged into udslib's `fn_security_seed` / `fn_security_key`
hooks. It is the 0x27 counterpart of [`../auth_challenge_mbedtls`](../auth_challenge_mbedtls)
(which does the same for 0x29 Authentication).

The seed→key relation demonstrated is exactly:

```
key = AES-128-CMAC(level_secret, seed)
```

with **two independent 16-byte secrets**, one per security level. The same
example builds against **two** crypto libraries — `make` (mbedTLS, default) or
`make CRYPTO=wolfssl` — and both produce **byte-identical** output through the
full flow. Only `aes_cmac()` and the linked library differ; udslib itself is
untouched. That is the point: the hook contract is bytes-in / bytes-out, so the
back-end is interchangeable.

## Wiring crypto into 0x27 — what the application owns, what the library owns

udslib owns all of the 0x27 **plumbing** and bundles **no cipher**:

- the requestSeed (odd sub-function) / sendKey (even sub-function) state machine,
- caching the issued seed and handing the exact bytes back to your key verifier,
- per-level sequencing — a key without a matching prior seed is rejected (NRC
  0x24, requestSequenceError),
- the failed-attempt counter and the lockout delay timer (NRC 0x36
  exceededNumberOfAttempts / 0x37 requiredTimeDelayNotExpired),
- NRC framing, and gating services by `ctx.security.level`.

The application owns the **crypto**, behind exactly two hooks:

```c
/* requestSeed: write the per-attempt nonce; the library caches it for you. */
static int on_security_seed(uds_ctx_t *ctx, uint8_t level,
                            uint8_t *seed_buf, uint16_t max_len);

/* sendKey: derive AES-128-CMAC(level_secret, seed) and constant-time compare.
 * Return 0 to unlock (library sets ctx.security.level = level), or a -NRC. */
static int on_security_key(uds_ctx_t *ctx, uint8_t level, const uint8_t *seed,
                           const uint8_t *key, uint16_t key_len);
```

Register them and you are done — no 0x27 logic in the application:

```c
cfg.fn_security_seed   = on_security_seed;
cfg.fn_security_key    = on_security_key;
cfg.security_max_attempts = 3u;       /* lock out after 3 bad keys ... */
cfg.security_delay_ms     = 10000u;   /* ... for 10 s (ISO 14229 C-15) */
```

The crypto itself lives in one helper, the only library-specific call site:

```c
static int aes_cmac(const uint8_t *key, const uint8_t *msg, size_t msg_len,
                    uint8_t out[16])
{
    const mbedtls_cipher_info_t *ci =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    return mbedtls_cipher_cmac(ci, key, 128u, msg, msg_len, out); /* keylen in bits */
}
```

The **wolfSSL** back-end (selected by `-DUSE_WOLFSSL`) is the same helper with
one different body (`wc_AesCmacGenerate`). To move to an **HSM/SHE**, replace it
with your key-handle CMAC call — `level_secret` becomes a handle the CPU can't
read, which is exactly why the crypto has to be a hook.

> **Caveat — "interchangeable" is not "zero-config".** The *code* swap is one
> function, but the library must actually ship the primitive **and be built with
> it**: wolfSSL needs `WOLFSSL_CMAC` + `WOLFSSL_AES_DIRECT` compiled in. And both
> ends must agree on the *scheme* — seed length, which secret per level, and the
> derivation (CMAC here; some OEMs use a challenge-response or HMAC). The udslib
> seam works regardless; the crypto choice is yours to match the tester.

## Why the library still doesn't bundle the cipher

- On a real ECU the key lives in an SHE/HSM the software cannot read, so a
  callback is the only integration that works — a bundled software AES couldn't
  even reach the key.
- A vetted crypto library (mbedTLS, wolfSSL, tinycrypt) or the HSM should own
  the primitives, not a protocol stack; bundling one enlarges the attack surface
  and the SBOM/CRA burden.
- License: udslib is PolyForm-Noncommercial; pulling third-party crypto into the
  core would entangle that. Linking it in *your* application (as here) does not.

The Authentication (0x29) `fn_auth` hook and the SecuredDataTransmission (0x84)
`fn_secure_decode` / `fn_secure_encode` hooks follow the identical pattern.

## The flow

```
(gated service 0xBA while locked)        -> NRC 0x33 securityAccessDenied
requestSeed level 1   (0x27 0x01)        -> server returns a 16-byte seed
sendKey level 1       (0x27 0x02)        -> client returns AES-CMAC(secret_l1, seed),
                                            server recomputes & compares (constant
                                            time) -> ctx.security.level = 1
(gated service 0xBA after unlock)        -> allowed
sendKey level 2 with a WRONG key         -> NRC 0x35 invalidKey, attempt counter++
sendKey level 2 with the right key       -> ctx.security.level = 2
```

The library auto-relocks (`security_level = 0`) on session change and reset.

## Real crypto, two honest shortcuts

The AES-CMAC derivation is real. Two things are fixed only to keep the output
deterministic, and both are marked in `main.c`:

- **The seed** is fixed (varied per level so the two levels are visibly
  distinct). In production fill it from your TRNG / HSM, e.g.
  `mbedtls_ctr_drbg_random(&drbg, seed_buf, 16)` — a fixed seed makes the
  seed→key relation replayable, which is exactly what you must NOT ship.
- **The level secrets** are test keys in flash. In production each is a key
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
=== 1. Gated service 0xBA while LOCKED -> NRC 0x33 (securityAccessDenied) ===
-> request: BA
  <- response: 7F BA 33

=== 2. requestSeed level 1 (0x27 0x01) ===
-> request: 27 01
  <- response: 67 01 B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 BA BB BC BD BE BF

=== 3. sendKey level 1 with AES-128-CMAC(secret_l1, seed) -> unlock ===
-> request: 27 02 27 DB ED 0B FB F3 C0 4C FA 52 5E E2 EC FD 5F EA
  <- response: 67 02
   ctx.security.level = 1

=== 4. Gated service 0xBA after unlock -> allowed ===
-> request: BA
  <- response: FA AC

=== 5. A WRONG key on level 2 -> NRC 0x35, attempt counter increments ===
-> request: 27 03
  <- response: 67 03 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F
-> request: 27 04 B4 00 2F B0 5A 2B 50 10 1F DD AB 3B 40 6D 70 36
  <- response: 7F 27 35

=== 6. Correct key on level 2 -> unlock level 2 ===
-> request: 27 03
  <- response: 67 03 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F
-> request: 27 04 4B 00 2F B0 5A 2B 50 10 1F DD AB 3B 40 6D 70 36
  <- response: 67 04
   ctx.security.level = 2
```
