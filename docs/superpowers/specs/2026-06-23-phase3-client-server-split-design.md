# Phase 3 — client/server split

**Date:** 2026-06-23
**Status:** Approved (design)
**Scope:** Phase 3 of the staged architecture decomposition (Phase 1 framework
emission + Phase 2 context regrouping shipped as v2.0.0, rebuilt on develop
1.20.0). This is the final planned phase.

## Background

udslib is a UDS **server** library. It also carries a small **client** role: the
same `uds_ctx_t` can send a request (`uds_client_request`) and have an incoming
response routed to a callback. After Phase 2 the client fields live in
`uds_ctx_t.client` (`cb`, `pending_sid`), and `uds_input_sdu_addr` intercepts a
frame whose SID matches `pending_sid | 0x40` (or a `0x7F` NRC for it) and fires
the callback instead of dispatching it as a server request.

This entangles two roles in one runtime context and one dispatch path. A
`test_role_isolation` test exists precisely because the shared `pending_sid`
once let a server async response be mistaken for a client response. Real
consumers: `examples/client_demo` and `examples/h5_uds_tester` (an FDCAN OTA
tester).

## Goal

Move the client role **out of** the server context into a dedicated
`uds_client_ctx_t` + module, leaving the server kernel pure: no client fields in
`uds_ctx_t`, no response-intercept branch in the server dispatch loop. The
client cannot corrupt server state by construction.

**Non-goals:** changing the wire protocol; the 0x84 `tx_buffer` sharing hazard
(still deferred); adding new client features. The client keeps exactly today's
capability (one outstanding request + a completion callback), just relocated.

## Design

### New unit: `include/uds/uds_client.h` + `src/services/uds_client.c`

```c
typedef struct uds_client_ctx {
    const uds_config_t *config; /* transport binding: tx_buffer, fn_tp_send, mutex */
    uint8_t pending_sid;        /* SID awaiting a response (0 = none) */
    uds_response_cb cb;         /* fired when the matching response arrives */
} uds_client_ctx_t;

/* Send a request and arm the completion callback. Builds {sid, data...} in
 * config->tx_buffer and sends via config->fn_tp_send. */
int uds_client_request(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data,
                       uint16_t len, uds_response_cb cb);

/* Feed an incoming frame. If it is the response to the outstanding request
 * (positive sid == pending|0x40, or a 0x7F NRC for pending), fire the callback,
 * clear the pending state, and return true (frame consumed). Otherwise return
 * false (the caller should route it elsewhere, e.g. a server uds_input_sdu). */
bool uds_client_handle_response(uds_client_ctx_t *c, uint8_t sid,
                                const uint8_t *data, uint16_t len);
```

The client reuses `uds_config_t` for its transport fields only (`tx_buffer`,
`tx_buffer_size`, `fn_tp_send`, `fn_mutex_lock`/`fn_mutex_unlock`,
`mutex_handle`). One `uds_config_t` can back both a server `uds_ctx_t` and a
client `uds_client_ctx_t`. Reusing the existing config (rather than a new
minimal transport struct) avoids a second config type and matches how consumers
already wire transport; the client touches none of the server hook fields.

`uds_response_cb` moves to `uds_client.h` and its first parameter changes from
`uds_ctx_t *` to `uds_client_ctx_t *` (breaking for client-callback authors —
all consumers are ours).

### Server kernel changes (`uds_config.h`, `uds_core.c`, `uds_core.h`)

- Remove `uds_client_state_t` and the `client` member from `uds_ctx_t`.
- Move `uds_response_cb` and `uds_client_request` declarations out of
  `uds_core.h` into `uds_client.h`.
- Delete `uds_client_request` and the response-intercept block
  (`if (ctx->client.pending_sid != 0u) { ... }`) from `uds_core.c`'s
  `uds_input_sdu_addr`. The extracted logic moves verbatim into
  `uds_client_handle_response`.

### Routing change (the breaking part)

Today an app feeds **all** frames to `uds_input_sdu`, which disambiguates
request-vs-response. After the split the app routes explicitly:

```c
if (!uds_client_handle_response(&client, sid, data, len)) {
    uds_input_sdu(&server, frame, frame_len); /* not my response -> server */
}
```

A pure tester (`h5_uds_tester`, `client_demo`) only uses the client side. The
`consumed` return keeps a dual-role app a one-liner.

### Consumers

- `client_demo`, `h5_uds_tester`: use `uds_client_ctx_t` + `uds_client_request`;
  route incoming frames through `uds_client_handle_response`. The callback
  signature updates to `uds_client_ctx_t *`.
- `test_role_isolation`: the "server async vs client response" confusion is now
  structurally impossible (separate types). Re-scope it to assert the server
  `uds_ctx_t` has no client state and a separate `uds_client_ctx_t` matches
  responses independently.

### Testing

- Server behaviour is unchanged except it no longer intercepts responses (that
  exact logic relocates). All existing server tests stay green, byte-identical.
- New `tests/unit/test_client.c`: over a mock transport — request is framed and
  sent; a positive response (`sid|0x40`) fires the cb with the payload and
  returns consumed=true; a `0x7F` NRC for the pending SID fires the cb and
  returns consumed=true; a non-matching frame returns consumed=false and leaves
  pending intact; a second response after completion returns consumed=false.

## Blast radius

- New: `include/uds/uds_client.h`, `src/services/uds_client.c`, CMake entry,
  `tests/unit/test_client.c`.
- `include/uds/uds_config.h` (drop client sub-struct), `include/uds/uds_core.h`
  (move client decls), `src/core/uds_core.c` (remove client code +
  response-intercept).
- `examples/client_demo/main.c`, `examples/h5_uds_tester/firmware/main.c`,
  `tests/unit/test_role_isolation.c`.
- CHANGELOG 2.0.0 "Breaking": client role moved to `uds_client_ctx_t`; apps
  route responses via `uds_client_handle_response`.

## Regression safety

1. Add `uds_client.h`/`uds_client.c` with the moved logic; wire into CMake.
   Build the library.
2. Remove the client surface from the server (`uds_ctx_t`, `uds_core.c`,
   `uds_core.h`); the build breaks until consumers/tests migrate.
3. Migrate `test_role_isolation` + add `test_client.c`; full suite green.
4. Migrate `client_demo` + `h5_uds_tester`; examples build/run.
5. Full Docker gate (cppcheck + clang-format-18 + MISRA + examples) at the end.

Gate: server tests byte-identical; the only new expected bytes are in the new
client test. Keep synced with develop (merge before the final PR).
