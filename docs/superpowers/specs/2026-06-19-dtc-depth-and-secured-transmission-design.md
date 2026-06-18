# Design: Structured DTC Depth + Secured Data Transmission (0x84)

**Date:** 2026-06-19
**Status:** Proposed
**Origin:** Issue #31 — assessment of CHARON-Server. Two ideas worth a native (clean-room, GPL-free) implementation: deeper DTC/stored-data sub-function coverage, and a secured-session / SecuredDataTransmission concept.

> **License note:** CHARON is GPLv3; udslib is PolyForm Noncommercial. No CHARON code is copied. We borrow only the *design shape* (a service-gating "encryption" column; structured DTC records). Everything here is written from the ISO 14229-1 spec and udslib's existing patterns.

This spec covers two **independent** features. Each ships as its own PR with its own implementation plan. Part A (DTC) is lower-risk and goes first.

---

## Part A — Structured DTC / Stored-Data Depth

### Problem
Today `fn_dtc_read(ctx, subfn, out_buf, max_len)` delegates **all** 0x19 (ReadDTCInformation) wire-formatting to the application. The app must emit ISO-correct bytes for every subfunction itself. This is error-prone and is the gap CHARON fills with ~2,200 LOC of stored-data logic. We want the library to own spec-correctness for the common subfunctions while keeping storage in the app.

### Approach (chosen): structured records, library formats the wire
The app supplies **structured DTC data** through new callbacks; the library formats the ISO 14229-1 0x19 response layout. Storage/persistence stays in the app (lean philosophy — no in-lib DTC store).

### Subfunctions owned by the library (phase 1)
| Sub | Name | Library formats |
|-----|------|-----------------|
| 0x01 | reportNumberOfDTCByStatusMask | status-availability-mask + count (filtered by mask) |
| 0x02 | reportDTCByStatusMask | list of {DTC(3B), statusByte} filtered by mask |
| 0x04 | reportDTCSnapshotRecordByDTCNumber | snapshot records for a DTC |
| 0x06 | reportDTCExtDataRecordByDTCNumber | extended-data records for a DTC |
| 0x0A | reportSupportedDTC | full DTC list |

Other subfunctions fall through to the existing raw `fn_dtc_read` (back-compat, no breakage).

### New config callbacks (additive; existing `fn_dtc_read` retained)
```c
/* Iterator model: library calls these to walk the app's DTC store. */
typedef struct {
    uint32_t dtc;        /* 3-byte DTC, right-aligned */
    uint8_t  status;     /* statusOfDTC byte (ISO 14229-1 Annex D) */
} uds_dtc_record_t;

/* Return count of DTCs matching status_mask; fill `out` up to max (0=count only). */
int (*fn_dtc_list)(struct uds_ctx *ctx, uint8_t status_mask,
                   uds_dtc_record_t *out, uint16_t max);

/* Snapshot (0x04) / extended-data (0x06) record bytes for one DTC+record-number. */
int (*fn_dtc_snapshot)(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                       uint8_t *out_buf, uint16_t max_len);
int (*fn_dtc_extdata)(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                      uint8_t *out_buf, uint16_t max_len);

/* statusAvailabilityMask reported in 0x01/0x02/0x0A responses. */
uint8_t dtc_status_availability_mask;
```

### Behaviour / error handling
- Subfunctions 0x01/0x02/0x0A require a status mask byte (already validated in `uds_internal_handle_read_dtc_info`); reuse that check.
- If the structured callback for an owned subfunction is NULL, fall back to `fn_dtc_read`; if that is also NULL → NRC 0x22 (ConditionsNotCorrect) as today.
- Buffer overrun: if formatting would exceed `tx_buffer_size`, return NRC 0x14 (ResponseTooLong) — currently the app's burden; library now enforces it.
- DTC format byte (SAE_J2012 etc.) is out of scope; we emit the records as supplied.

### Tests (cmocka, host_sim)
- `test_service_dtc.c` extended: 0x01 count math vs mask, 0x02 filtered list, 0x04/0x06 record passthrough, 0x0A full list, ResponseTooLong truncation, NULL-callback fallback to legacy `fn_dtc_read`.

---

## Part B — Secured Data Transmission (0x84) + Secured Session

### Problem
udslib has no concept of a secured channel. CHARON sketches one (a `SESSION_SECURED` type + an "encryption" column gating which SIDs require a secured channel + encode/decode hooks) but the crypto itself is an unimplemented comment. We implement it properly.

### Approach (chosen): hooks primary + optional bundled AES-CMAC
1. **Hooks are the interface.** The library owns 0x84 framing, the Administrative Parameter (APAR), the secured-session gate, and request/response wrapping. The app supplies the crypto via callbacks.
2. **Optional reference crypto.** Behind `UDS_ENABLE_BUILTIN_CRYPTO` (default OFF), ship a clean-room AES-CMAC (auth) and optional AES-GCM (encrypt) that simply *wire into* the same hooks, so 0x84 works out-of-box without app crypto. The embeddable core stays dependency-free when the flag is off.

### Secured session
- New session bit `UDS_SESSION_SECURED (1 << 3)` added to the existing mask scheme.
- Service entries / `user_services` may set this bit to require a secured channel; `is_session_supported()` already gates on the active-session bit — extend `uds_internal_session_bit()` to map the secured state.
- Entering the secured session: via 0x84 establishment or a successful Authentication (0x29) — exact trigger configurable; default = "0x84 with valid APAR + verify".

### 0x84 framing (ISO 14229-1 §10.x SecuredDataTransmission)
Request:  `84 <APAR(2B)> [signature/MAC | encrypted payload] <inner UDS request>`
The library:
1. Parses APAR (request/response, signed/encrypted, pre-established-key bits).
2. Calls `fn_secure_decode` to verify/decrypt → recovers inner UDS request bytes.
3. Dispatches the inner request through the normal service table (respecting `UDS_SESSION_SECURED` gating).
4. Calls `fn_secure_encode` to sign/encrypt the inner response.
5. Wraps as `C4 <APAR> <secured response>`.

### New config callbacks
```c
/* Verify+decrypt an incoming secured message. Returns inner-request length
 * written to `out`, or negative NRC (e.g. 0x38..0x4F secured-area range). */
int (*fn_secure_decode)(struct uds_ctx *ctx, uint16_t apar,
                        const uint8_t *in, uint16_t in_len,
                        uint8_t *out, uint16_t out_max);

/* Sign+encrypt an outgoing inner response. Returns secured length. */
int (*fn_secure_encode)(struct uds_ctx *ctx, uint16_t apar,
                        const uint8_t *in, uint16_t in_len,
                        uint8_t *out, uint16_t out_max);
```
When `UDS_ENABLE_BUILTIN_CRYPTO` is set and these are NULL, the library installs its AES-CMAC/GCM defaults (key supplied via a `secure_key`/`secure_key_len` config field).

### Behaviour / error handling
- 0x84 in a session that does not permit it → NRC 0x7F (serviceNotSupportedInActiveSession).
- APAR inconsistent / unsupported bits → NRC 0x13 (incorrectMessageLength) or 0x31 (requestOutOfRange) per ISO.
- `fn_secure_decode` failure (bad MAC) → NRC from the secured range (general 0x33 securityAccessDenied as a safe default; exact code returned by the hook).
- Inner request must NOT itself be 0x84 (no recursion) → NRC 0x31.
- Suppress-positive-response on the inner request is honored.

### Tests (cmocka, host_sim)
- `test_service_84.c` (new): APAR parse, decode→dispatch→encode round-trip with a stub hook, secured-session gating (a SECURED-only SID rejected outside secured session, accepted inside), bad-MAC rejection, recursion rejection, suppress-positive.
- `test_crypto_cmac.c` (new, only when `UDS_ENABLE_BUILTIN_CRYPTO`): AES-CMAC test vectors (NIST SP 800-38B), end-to-end 0x84 with built-in crypto.

---

## Cross-cutting

- **Back-compat:** all new callbacks/fields are additive; existing configs compile and behave unchanged. New session bit does not alter existing masks (`UDS_SESSION_ALL = 0xFF` already includes it).
- **MISRA:** follow `docs/MISRA.md`; explicit casts, no implicit narrowing, bounded loops in DTC iteration.
- **Format gate:** clang-format-14 (CI gate; local v22 reformats differently — verify with v14).
- **Branch/PR:** branch off `develop`; two PRs (Part A, then Part B) targeting `develop`.
- **Version:** Part A → 1.15.0 (new feature, additive). Part B → 1.16.0.
- **Docs:** update `SERVICE_COMPLIANCE.md` (0x19 subfunction coverage, 0x84 row) and `UDS_SERVER_OPTIONS.md` (new callbacks, `UDS_ENABLE_BUILTIN_CRYPTO`).

## Out of scope
- In-library DTC persistence/NVM store (storage stays in app).
- DTC format-identifier translation (SAE_J2012 vs ISO_14229-1).
- Certificate-based key exchange for 0x84 (use Authentication 0x29 + app crypto).
- ResponseOnEvent-driven secured messages.
