# Framework-owned response emission (result-descriptor handler contract)

**Date:** 2026-06-22
**Status:** Approved (design)
**Scope:** Phase 1 of a staged architecture decomposition. Phases 2 and 3 are
outlined here but specified and executed separately.

## Background

Two ISO-compliance bugs (#76, #80) were filed and fixed in the ECU Reset (0x11)
handler. Both shared one root cause: **response emission, positive-response
suppression, and post-response side-effect ordering are hand-coded inside every
service handler instead of being owned by the dispatch framework.**

Evidence:
- `suppress_pos_resp` is checked-and-early-returned in ~13 places across the
  service handlers; a handler that suppresses and returns without calling
  `uds_send_response()` leaked the flag into the next request (#80).
- The 0x11 handler hand-ordered "perform reset, then send response", which is
  backwards per ISO 14229-1 — the response must be on the wire before the reset
  executes (#76).
- ~182 direct `uds_send_response()` / `uds_send_nrc()` call sites across 9
  service files; each handler independently re-implements the
  build-payload → check-suppress → emit ritual.

The acute bugs are already fixed and merged. This refactor removes the *class*
of bug by moving emission, suppression, and ordering into the framework.

## Goal

A single, framework-owned response-emission path. Handlers describe *what* to
respond with; the framework decides *how and when* it reaches the wire.

Non-goals (this phase): changing wire behaviour, transport, or any ISO
semantics. Output bytes for every existing test must be byte-identical.

## Design

### Handler contract (breaking change)

Replace the current contract:

```c
typedef int (*uds_service_handler_t)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);
```

with a result-descriptor return:

```c
typedef enum {
    UDS_RESULT_POSITIVE, /* tx_buffer[0..len-1] holds the positive response   */
    UDS_RESULT_NRC,      /* emit negative response with .nrc                  */
    UDS_RESULT_PENDING,  /* async: framework emits 0x78 and tracks P2*        */
    UDS_RESULT_NONE      /* emit nothing (e.g. functional-broadcast drop)     */
} uds_result_kind_t;

typedef enum {
    UDS_AFTER_NONE = 0,
    UDS_AFTER_RESET      /* invoke fn_reset(after_arg) AFTER the response      */
} uds_after_action_t;

typedef struct {
    uds_result_kind_t  kind;
    uint16_t           len;       /* payload length in tx_buffer (POSITIVE)   */
    uint8_t            nrc;       /* NRC code (NRC)                           */
    uds_after_action_t after;     /* deferred side-effect, run after emission */
    uint8_t            after_arg; /* argument for the deferred action         */
} uds_result_t;

typedef uds_result_t (*uds_service_handler_t)(struct uds_ctx *ctx,
                                              const uint8_t *data, uint16_t len);
```

Handlers continue to write their positive payload into
`ctx->config->tx_buffer` exactly as today. Instead of calling
`uds_send_response()` / `uds_send_nrc()`, they return a descriptor built with
thin inline helpers:

```c
static inline uds_result_t uds_ok(uint16_t len);                 /* POSITIVE   */
static inline uds_result_t uds_nrc(uint8_t nrc);                 /* NRC        */
static inline uds_result_t uds_pending(void);                    /* PENDING    */
static inline uds_result_t uds_none(void);                       /* NONE       */
static inline uds_result_t uds_ok_then_reset(uint16_t len, uint8_t sub);
```

### Framework emission (single authority)

`execute_handler()` becomes the only place that emits:

```c
uds_result_t r = service->handler(ctx, data, len);

switch (r.kind) {
case UDS_RESULT_PENDING:
    uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
    ctx->p2_msg_pending  = true;
    ctx->p2_star_active  = true;
    ctx->p2_timer_start  = ctx->config->get_time_ms();
    ctx->server_pending_sid = data[0];
    break;
case UDS_RESULT_NRC:
    /* NRC is never suppressed (ISO 14229-1). Centralized here. */
    uds_send_nrc(ctx, data[0], r.nrc);
    break;
case UDS_RESULT_POSITIVE:
    if (ctx->suppress_pos_resp) {
        ctx->suppress_pos_resp = false;   /* consumed; single suppress site */
        ctx->rcrrp_count = 0u;
        if (ctx->secure_capturing) ctx->secure_capture_len = 0u;
    } else {
        uds_emit_response(ctx, r.len);    /* pure transport / secure-capture */
    }
    break;
case UDS_RESULT_NONE:
    break;
}

/* Deferred side-effects run only after the response is on the wire. */
if (r.after == UDS_AFTER_RESET && ctx->config->fn_reset) {
    ctx->config->fn_reset(ctx, r.after_arg);
}
return /* status for callers */;
```

Outcomes:
- **One** suppress-consumption site instead of 13.
- NRC-not-suppressed rule lives in exactly one place.
- Post-response ordering (reset) is declared, not hand-sequenced → #76 cannot
  recur in any handler.

### Emission helper split

`uds_send_response()` currently mixes three concerns: suppress check, secure
capture, and transport. Split into:

- `uds_emit_response(ctx, len)` — pure: routes to `secure_capture` when
  `secure_capturing`, otherwise `fn_tp_send`; resets `rcrrp_count`. No suppress
  logic.
- `uds_send_response(ctx, len)` — **kept as a public compatibility helper**:
  `if (suppress) { consume; return; } else uds_emit_response()`. External code
  calling it directly keeps working. Core handlers no longer call it.

`uds_send_nrc()` is unchanged and remains public.

### Secure data transmission (0x84)

The inner request is dispatched through `handle_request` → `execute_handler`,
so the new emission switch handles capture automatically: when
`secure_capturing` is set, `uds_emit_response()` writes into the capture buffer
and a suppressed inner POSITIVE zeroes `secure_capture_len`, identical to today.

## Blast radius

- Public typedef change → **breaking**. `config.user_services` handlers must
  return `uds_result_t`. Major version bump (v1.x → **v2.0.0**).
- 9 service files, ~182 `uds_send_*` call sites converted to `return uds_*(…)`.
- `examples/custom_service` migrated to the new contract.
- Any cmocka test that defines a handler (e.g. via `user_services`) migrated.
- Phase 1 is necessarily a **single atomic PR**: the signature change does not
  compile until all handlers are migrated, which guarantees no half-migrated
  state can ship.

## Testing

- All 59 existing tests must stay green; output bytes byte-identical (the
  refactor is behaviour-preserving).
- The #76 (`test_ecu_reset_response_sent_before_reset`) and #80
  (`test_ecu_reset_suppress_does_not_leak`) regression tests are the canaries.
- Add a focused test asserting `UDS_RESULT_NONE` emits nothing and that a
  `user_services` handler returning each `uds_result_kind_t` behaves correctly.
- `clang-format` 18 (CI Docker image) clean; CHANGELOG updated with the
  migration note.

## Later phases (separate specs)

- **Phase 2 — context regrouping.** Group `uds_ctx_t`'s ~30 flat fields into
  `server` / `client` / `transport` / `security` / `session` sub-structs and
  separate per-dispatch scratch from persistent state, making lifetimes
  explicit. Non-breaking if accessor macros are used; otherwise a follow-on
  major.
- **Phase 3 — client/server split.** Move the client-role fields
  (`client_cb`, `client_pending_sid`) and request API out of the server
  context into a distinct type.

Each later phase ships only after the previous is merged and green; stop early
if the marginal value plateaus.
```
