# Phase 2 — context regrouping (uds_ctx_t sub-structs)

**Date:** 2026-06-22
**Status:** Approved (design)
**Scope:** Phase 2 of the staged architecture decomposition begun in
`2026-06-22-framework-owned-response-emission-design.md` (Phase 1, shipped as
v2.0.0 on this branch). Phase 3 (client/server split) remains separate.

## Background

Phase 1 moved response emission/suppression/ordering into the dispatch
framework, removing the #76/#80 bug class by discipline (a single suppress
site). It deliberately left `uds_ctx_t` as ~40 flat fields and named two
follow-ups: grouping the context into sub-structs, and separating per-dispatch
scratch from persistent state.

This phase does that grouping. It is **organizational and defense-in-depth**,
not a new capability or a live bug fix:

- The #80 flag-leak class is already fixed in Phase 1. Grouping the
  per-dispatch fields into a `scratch` sub-struct and resetting the
  dispatch-scoped subset at top-level entry makes that class impossible *by
  construction* rather than by discipline.
- The 0x84 "shared `tx_buffer` aliasing" is benign at the one nesting level the
  library permits: the inner response is copied out to a stack buffer before
  the outer writes its `0xC4` response, and a nested 0x84 is rejected with
  requestOutOfRange. Phase 2 does **not** change this; it makes the
  non-reentrant contract explicit and documented. We do not claim a
  memory-safety fix where none is needed.

The honest value: a kernel whose state lifetimes are visible in the type, and a
structural guard against per-request flag leaks.

## Goal

Group `uds_ctx_t`'s runtime fields into named sub-structs by lifetime/role,
with **zero wire-behaviour change** (byte-identical output for every existing
test). Reset the dispatch-scoped scratch at top-level request entry so a
per-request flag cannot leak into the next request.

**Non-goals (this phase):** changing any ISO semantics, transport, the public
handler contract (`uds_ok`/`uds_nrc`/`uds_pending`/`uds_none` stay), the
config struct (`uds_config_t`, including `tx_buffer`), or the client/server
split (Phase 3). No new buffers, no reentrancy support.

## Design

### Layout

```c
struct uds_ctx {
    const uds_config_t *config;          /* unchanged */

    struct uds_session_state    session;   /* diagnostic session + S3/P2 timing */
    struct uds_security_state   security;  /* access level, attempts, delay, seed */
    struct uds_server_state     server;    /* async response engine + service state */
    struct uds_client_state     client;    /* request-side role (Phase 3 lives here) */
    struct uds_dispatch_scratch scratch;   /* per-request, non-persistent */
};
```

Named (not C11 anonymous) sub-structs, so the grouping is explicit and
greppable. Field names are de-prefixed where the sub-struct makes the prefix
redundant.

| Sub-struct | Type | Fields (final names) |
|---|---|---|
| `session` | `uds_session_state` | `active`, `last_msg_time`, `p2_ms`, `p2_star_ms`, `p2_server_max`, `p2_star_server_max`, `comm_state` |
| `security` | `uds_security_state` | `level`, `authenticated`, `attempts`, `delay_end`, `seed_level`, `seed_len`, `seed[UDS_SECURITY_SEED_MAX]` |
| `server` | `uds_server_state` | `p2_timer_start`, `p2_msg_pending`, `p2_star_active`, `pending_sid`, `rcrrp_count`, `flash_sequence`, `link_ctrl_verified`, `link_ctrl_param`, `periodic_ids[8]`, `periodic_rates[8]`, `periodic_timers[8]`, `periodic_count` |
| `client` | `uds_client_state` | `cb` (`void *`), `pending_sid` |
| `scratch` | `uds_dispatch_scratch` | `suppress_pos_resp`, `req_addr_mode`, `reset_pending`, `reset_pending_type`, `in_secured_session`, `secure_capturing`, `secure_capture_buf`, `secure_capture_size`, `secure_capture_len`, `secure_capture_overflow` |

The sub-struct type definitions are internal (declared where `struct uds_ctx`
is defined, in `include/uds/uds_config.h`). They are not part of the stable API
contract — the CHANGELOG already states the `uds_ctx_t` field layout is not API.

### Scratch reset (the one subtle part)

`scratch` holds two lifetimes:

- **Dispatch-scoped:** `suppress_pos_resp`, `req_addr_mode`. Reset to zero at
  **top-level request entry only** — in `uds_input_sdu` / `uds_input_sdu_addr`,
  before `handle_request` runs. NOT in `handle_request` itself.
- **Nested-capture:** `secure_capturing`, `secure_capture_*`, `in_secured_session`.
  Managed explicitly by the 0x84 handler (`uds_internal_handle_secured_data`):
  set up → dispatch inner via `handle_request` → read `secure_capture_len` /
  `secure_capture_overflow` as outputs → tear down. Unchanged from Phase 1.
- `reset_pending` / `reset_pending_type`: set by the 0x11 handler, consumed by
  `execute_handler` strictly after emission (Phase 1 behaviour, unchanged).

**Why not a blanket `memset(&ctx->scratch, 0, ...)` in `handle_request`:** the
0x84 handler sets `scratch.secure_capturing = true` and then calls
`handle_request(inner)`. A memset at `handle_request` entry would wipe the
outer's capture state mid-flight and break 0x84. Top-level-only reset preserves
the nested dispatch's view. This nested lifetime is documented at both the
reset site and the 0x84 capture site.

The existing per-dispatch clear of `suppress_pos_resp` at the top of
`handle_request` (Phase 1) is replaced by the top-level scratch reset; the
inner 0x84 dispatch must still clear its own `suppress_pos_resp` so a suppressed
inner request behaves correctly — verify against the existing 0x84 suppress
tests (byte-identical).

### Behaviour preservation

- Output bytes for every existing test are byte-identical. If a test's expected
  bytes would change, that is a defect — stop and investigate, do not edit the
  expectation.
- The change is a global field rename; it will not compile half-done, so the
  struct change + all in-tree accessor updates land together per file, gated by
  the build.

## Blast radius

- `include/uds/uds_config.h`: struct definition + 5 sub-struct types.
- All 10 `src/services/*.c` + `src/core/uds_core.c` + transport call sites:
  every `ctx->FIELD` → `ctx->GROUP.FIELD`.
- ~20 test files: every `ctx.FIELD` / `g_ctx.FIELD` → `ctx.GROUP.FIELD`.
- Examples that read ctx fields directly (audit; most use callbacks, not raw
  fields).
- Same unreleased v2.0.0 — no additional version bump. CHANGELOG 2.0.0
  "Breaking" extended with a one-line "internal context fields regrouped into
  sub-structs (not API; see migration table)" note.

## Regression safety

1. Define the 5 sub-struct types and the new `struct uds_ctx`; migrate
   `src/core` first (the dispatch + emission core), build, run full suite.
2. Migrate service files (group by file), then transport. After each file:
   full suite green + clang-format-18 + cppcheck + MISRA clean.
3. Migrate tests and examples. Full suite + all examples build/run green.
4. Add the top-level scratch reset; add a regression test proving a
   dispatch-scoped flag set in one request does not survive into the next
   (defense-in-depth lock for the #80 class).
5. Byte-identical guard throughout. The #76/#80 and 0x84 suppress/capture
   tests are the canaries.

Gate at every commit: full ctest suite, 0 build warnings, clang-format-18,
cppcheck, MISRA (`check_misra.sh`), and — for the final commit — all host
examples build/run (the nightly-examples command).

## Later phases

- **Phase 3 — client/server split.** Move `client.*` and the request API out of
  the server context entirely. The `client` sub-struct introduced here is the
  seam for that work.
