# Authentication (0x29) example

Shows how to implement an **Authentication** challenge / proof-of-ownership
flow through udslib's `fn_auth` hook — without the library bundling any crypto.

## Why a hook, not built-in crypto

udslib's 0x29 handler is plumbing: it validates the request, calls `fn_auth`,
and frames the `0x69` response. The authentication state machine and the
cryptography are the **application's** (the library bundles no cipher) because:

- on a real ECU the key lives in an SHE / HSM that software cannot read, so a
  callback is the only integration that works;
- a vetted crypto library (mbedTLS, wolfSSL, tinycrypt) or the HSM should own
  the primitives, not a protocol stack;
- keeping crypto out of the core minimises attack surface and SBOM/CRA burden.

The SecuredDataTransmission (0x84) `fn_secure_decode` / `fn_secure_encode` hooks
follow the identical pattern.

## The flow

```
(gated service 0xBA before auth)              -> NRC 0x34 authenticationRequired
verifyCertificateUnidirectional   (0x29 0x01) -> evaluation status
requestChallengeForAuthentication (0x29 0x05) -> server returns a challenge nonce
proofOfOwnership                  (0x29 0x03) -> client returns tag(challenge),
                                                 server verifies -> ctx.authenticated
(gated service 0xBA after auth)               -> allowed
```

`deAuthenticate` (0x29 0x00) and `authenticationConfiguration` (0x08) are handled
by the library natively. Services are gated on authentication via the
`fn_auth_required(ctx, sid)` config hook; when it returns true and the channel
is not authenticated, the library rejects with NRC 0x34. The library auto-clears
`ctx.authenticated` on deAuthenticate, session change, S3 timeout, and reset.

## ⚠️ The demo tag is not cryptography

`demo_tag()` is an FNV hash over `key || data`, present only to make the wiring
runnable and deterministic. **Replace it with AES-CMAC / HMAC from a vetted
library or your HSM in production** — the call sites are the two `demo_tag()`
uses in `handle_auth()`.

## Build & run

```sh
make run
```

Expected output:

```
=== 1. Gated service 0xBA BEFORE auth -> NRC 0x34 (authenticationRequired) ===
-> request: BA
  <- response: 7F BA 34
...
=== 4. proofOfOwnership with the correct tag -> verified ===
-> request: 29 03 D2 A4 D7 2B
  <- response: 69 03 02
   ctx.authenticated = true

=== 5. Gated service 0xBA AFTER auth -> allowed ===
-> request: BA
  <- response: FA AC
```
