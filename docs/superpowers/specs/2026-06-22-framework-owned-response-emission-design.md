# Framework-owned response emission (hardened result-descriptor contract)

**Date:** 2026-06-22
**Status:** Approved (design, hardened after self-roast)
**Scope:** Phase 1 of a staged architecture decomposition. Phases 2 and 3 are
outlined here but specified and executed separately.

## Background

Two ISO-compliance bugs (#76, #80) were filed and fixed in the ECU Reset (0x11)
handler. Both shared one root cause: **response emission, positive-response
suppression, and post-response side-effect ordering are hand-coded inside every
service handler instead of being owned by the dispatch framework.**

Evidence:
- `suppress_pos_resp` is checked-and-early-returned in ~13 places; a handler
  that suppressed and returned without calling `uds_send_response()` leaked the
  flag into the next request (#80).
- The 0x11 handler hand-ordered "perform reset, then send response", which is
  backwards per ISO 14229-1 (#76).
- ~182 direct `uds_send_response()` / `uds_send_nrc()` call sites across 9
  service files; each handler re-implements build → check-suppress → emit.

The acute bugs are already fixed and merged. This refactor removes the *class*
of bug by moving emission, suppression, and ordering into the framework.

## Goal

A single, framework-owned response-emission path. Handlers describe *what* to
respond with; the framework decides *how and when* it reaches the wire.

**Non-goals (this phase):** changing wire behaviour, transport, or any ISO
semantics. **Output bytes for every existing test must be byte-identical** —
this is a behaviour-preserving refactor.

## Design (hardened)

The first draft of this design was self-roasted; five weaknesses were found and
fixed here:

1. `after_action` enum was a junk drawer → **removed**, replaced by a single
   `bool reset_pending`.
2. `UDS_RESULT_NONE` was a "respond with nothing" footgun → **retained for exactly
   one sanctioned use**: the 0x84 `uds_internal_handle_secured_data` path where
   the inner response was suppressed (`captured_len == 0`). That path is a genuine
   no-response case (the original code did `return UDS_OK` emitting nothing; the
   refactor must preserve that behaviour). `execute_handler` treats `UDS_RESULT_NONE`
   as a no-op (`case UDS_RESULT_NONE: break`), which is byte-identical to the
   original. Every other suppress early-return is handled centrally as
   POSITIVE+suppress; `UDS_RESULT_NONE` is not a general escape hatch.
3. By-value struct return invited MISRA/ABI debate → **out-parameter** instead.
4. The atomic mega-PR's "byte-identical" claim rested on a test suite with a
   known suppress-path gap → **a prerequisite test-coverage step** closes the
   gap *before* any handler is touched.
5. The shared-`tx_buffer` / secure-capture hazard is the real deep problem →
   **explicitly scoped out** and named as the Phase 2 target, so we are honest
   that Phase 1 does not touch the scariest state.

### Handler contract (breaking change)

Replace:

```c
typedef int (*uds_service_handler_t)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);
```

with an out-parameter form that writes the response description into `*out`
(void return — there is no separate status; an internal error is expressed as
`UDS_RESULT_NRC`):

```c
typedef enum {
    UDS_RESULT_POSITIVE, /* tx_buffer[0..len-1] holds the positive response */
    UDS_RESULT_NRC,      /* emit negative response with .nrc                */
    UDS_RESULT_PENDING   /* async: framework emits 0x78 and tracks P2*      */
} uds_result_kind_t;

typedef struct {
    uds_result_kind_t kind;
    uint16_t          len; /* payload length in tx_buffer (POSITIVE) */
    uint8_t           nrc; /* NRC code (NRC)                         */
} uds_result_t;

typedef void (*uds_service_handler_t)(struct uds_ctx *ctx, const uint8_t *data,
                                      uint16_t len, uds_result_t *out);
```

Handlers write their positive payload into `ctx->config->tx_buffer` exactly as
today, then set `*out` via thin inline helpers instead of calling
`uds_send_response()` / `uds_send_nrc()`:

```c
static inline void uds_ok(uds_result_t *out, uint16_t len);  /* POSITIVE */
static inline void uds_nrc(uds_result_t *out, uint8_t nrc);  /* NRC      */
static inline void uds_pending(uds_result_t *out);           /* PENDING  */
```

There are four kinds: POSITIVE, NRC, PENDING, and NONE. NONE is sanctioned only
for the 0x84 suppressed-inner path; no other handler may use it.

### Post-emit side effects

Audit result: only two handlers perform work after sending today —
`fn_reset` (0x11) and `fn_nvm_save` (0x10). Only **reset must be post-emit**
(it reboots the MCU and destroys the ability to respond). `fn_nvm_save` has no
such constraint, so it **moves to before the descriptor is set** inside the
session handler — behaviour-preserving (the save runs unconditionally either
way, with identical state bytes) and removes it from the "ordering-sensitive"
category entirely.

Reset is handled by a single flag:

```c
/* set by the 0x11 handler instead of calling fn_reset itself */
ctx->reset_pending      = true;
ctx->reset_pending_type = sub;
```

consumed by the framework strictly after emission (see below). One bool for the
one genuinely ordering-critical case; if a future service needs post-emit
ordering it gets the same scrutiny, not a shared enum.

### Framework emission (single authority)

`execute_handler()` becomes the only place that emits:

```c
uds_result_t r;
service->handler(ctx, data, len, &r);

switch (r.kind) {
case UDS_RESULT_PENDING:
    uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
    ctx->p2_msg_pending     = true;
    ctx->p2_star_active     = true;
    ctx->p2_timer_start     = ctx->config->get_time_ms();
    ctx->server_pending_sid = data[0];
    break;
case UDS_RESULT_NRC:
    uds_send_nrc(ctx, data[0], r.nrc);   /* NRC is never suppressed (ISO) */
    break;
case UDS_RESULT_POSITIVE:
    if (ctx->suppress_pos_resp) {        /* THE single suppress site */
        ctx->suppress_pos_resp = false;
        ctx->rcrrp_count       = 0u;
        if (ctx->secure_capturing) ctx->secure_capture_len = 0u;
    } else {
        uds_emit_response(ctx, r.len);   /* pure transport / secure-capture */
    }
    break;
}

/* Deferred reset runs only after the response is on the wire. */
if (ctx->reset_pending) {
    ctx->reset_pending = false;
    if (ctx->config->fn_reset) ctx->config->fn_reset(ctx, ctx->reset_pending_type);
}
```

Outcomes: one suppress site (was 13); NRC-not-suppressed in one place; reset
ordering structurally guaranteed (#76 cannot recur); no growing abstraction.

### Emission helper split

`uds_send_response()` currently mixes suppress check + secure capture +
transport. Split into:

- `uds_emit_response(ctx, len)` — pure: routes to secure capture when
  `secure_capturing`, else `fn_tp_send`; resets `rcrrp_count`. No suppress.
- `uds_send_response(ctx, len)` — **kept as a public compatibility helper**:
  `if (suppress) { consume; return; } else uds_emit_response()`. External
  callers keep working; core handlers no longer call it.

`uds_send_nrc()` unchanged, remains public.

### Secured data transmission (0x84)

The inner request is dispatched through `handle_request` → `execute_handler`,
so the emission switch handles capture automatically (suppressed inner POSITIVE
zeroes `secure_capture_len`, identical to today). The deeper `tx_buffer`
aliasing during nested capture is **not** addressed here — it is the Phase 2
target.

## Regression safety (each step verifiable)

The signature change is global, so the *handler migration* is necessarily one
atomic commit (it will not compile half-done — which itself prevents a
half-migrated ship). Regression safety comes from sequencing and gates, not
from splitting the uncompilable:

1. **Step 0 — close the test gap FIRST (separate commit, no production change).**
   Add suppress-path and side-effect-ordering tests for every sub-function
   service and every post-emit handler, so "byte-identical" is a *verified
   gate* before any handler changes. This step alone must pass on `develop`.
2. **Step 1 — introduce types + `uds_emit_response` + new `execute_handler`**,
   keeping `uds_send_response` as the compat shim. Full suite green.
3. **Step 2 — migrate handlers, one service file per commit.** Each commit must
   compile (the typedef flips in this step, so the first migration commit and
   the typedef change are one commit; remaining files follow). After **every**
   commit: full 59-test suite green + clang-format-18 clean. A file is not
   committed until its suite run is green.
4. **Step 3 — migrate `examples/custom_service` and any handler-defining
   tests.** Full suite + examples build green.
5. **Byte-identical guard:** no test's expected output bytes change. If a test
   would need new expected bytes, that is a behaviour change and a defect in the
   refactor — stop and investigate (do not edit the expectation).

Gate at every commit: 59/59 tests, clang-format-18 (CI Docker image) clean.
The #76/#80 regression tests are the canaries and must stay green throughout.

## Blast radius

- Public typedef change → **breaking**; `config.user_services` handlers must
  adopt the out-param form. **Major version bump (v1.x → v2.0.0)**; CHANGELOG
  migration note.
- 9 service files, ~182 `uds_send_*` call sites → set `*out`.
- `examples/custom_service` + handler-defining tests migrated.

## Later phases (separate specs)

- **Phase 2 — context regrouping + the `tx_buffer` hazard.** Group
  `uds_ctx_t`'s ~30 flat fields into `server` / `client` / `transport` /
  `security` / `session` sub-structs, separate per-dispatch scratch from
  persistent state, and address shared-buffer aliasing during 0x84 nesting.
- **Phase 3 — client/server split.** Move client-role fields (`client_cb`,
  `client_pending_sid`) and the request API out of the server context.

Each later phase ships only after the previous is merged and green; stop early
if the marginal value plateaus.
```
