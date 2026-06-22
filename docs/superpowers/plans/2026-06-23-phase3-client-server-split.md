# Phase 3 — client/server split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the client role out of `uds_ctx_t` into a dedicated `uds_client_ctx_t` + `uds_client.{h,c}`, leaving the server kernel pure (no client fields, no response-intercept in the dispatch loop).

**Architecture:** A new client module owns `pending_sid` + `cb` and reuses a `uds_config_t` for transport only. `uds_client_request` sends; `uds_client_handle_response` matches an incoming frame to the outstanding request and fires the callback. The server stops auto-routing responses; the app routes them.

**Tech Stack:** C11, cmocka, CMake, Docker CI (clang-format-18, cppcheck, MISRA), example Makefiles.

## Global Constraints

- **Server behaviour byte-identical** except the removal of response interception; all existing server tests stay green. The only new expected bytes are in the new client test.
- **No wire-protocol change.** `uds_config_t` (transport) unchanged; `uds_send_response`/`uds_send_nrc` untouched.
- **Client-mode transports receive `ctx == NULL`** from `fn_tp_send` (the client has no `uds_ctx_t`); both existing adapters already `(void) ctx;`.
- **Gate at every commit:** full ctest suite, 0 build warnings, clang-format-18, cppcheck, MISRA (`scripts/check_misra.sh`); final commit also: all host examples build+run.
- Commits authored as `w1ne` (`14119286+w1ne@users.noreply.github.com`); no Claude/AI references. Keep synced: `git merge origin/develop` before the final PR.

---

### Task 1: Client module + remove client surface from the server

**Files:**
- Create: `include/uds/uds_client.h`, `src/services/uds_client.c`
- Modify: `CMakeLists.txt` (add source), `include/uds/uds_config.h` (drop client sub-struct), `include/uds/uds_core.h` (move client decls out), `src/core/uds_core.c` (remove `uds_client_request` + response-intercept)

**Interfaces:**
- Produces: `uds_client_ctx_t`, `uds_response_cb` (now `void(*)(uds_client_ctx_t*,uint8_t,const uint8_t*,uint16_t)`), `uds_client_request`, `uds_client_handle_response` — consumed by Tasks 2-3.

- [ ] **Step 1: Create `include/uds/uds_client.h`**

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_CLIENT_H
#define UDS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "uds/uds_config.h"

struct uds_client_ctx;

/* Fired when the response to a uds_client_request() arrives.
 * data/len are the response payload AFTER the SID byte. */
typedef void (*uds_response_cb)(struct uds_client_ctx *c, uint8_t sid, const uint8_t *data,
                                uint16_t len);

/* UDS client role: one outstanding request + completion callback. Reuses a
 * uds_config_t for transport only (tx_buffer, fn_tp_send, mutex); the server
 * hook fields are unused. Independent of the server uds_ctx_t. */
typedef struct uds_client_ctx
{
    const uds_config_t *config; /* transport binding (server hooks unused) */
    uint8_t pending_sid;        /* SID awaiting a response (0 = none) */
    uds_response_cb cb;         /* fired on the matching response */
} uds_client_ctx_t;

/* Build {sid, data...} in config->tx_buffer and send via config->fn_tp_send
 * (called with ctx == NULL), then arm cb. Returns the transport result or a
 * negative UDS_ERR_*. */
int uds_client_request(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb cb);

/* Feed an incoming frame. If it is the response to the outstanding request
 * (positive sid == pending|0x40, or a 0x7F negative response echoing pending),
 * fire cb with the payload after the SID, clear pending, and return true.
 * Otherwise return false (caller routes the frame elsewhere). */
bool uds_client_handle_response(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data,
                                uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CLIENT_H */
```

- [ ] **Step 2: Create `src/services/uds_client.c`**

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <string.h>

#include "uds/uds_client.h"
#include "uds/uds_core.h" /* UDS_ERR_*, UDS_OK */

#define UDS_CLIENT_RESPONSE_OFFSET 0x40u
#define UDS_CLIENT_NEG_RESPONSE_SID 0x7Fu

int uds_client_request(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb cb)
{
    if ((c == NULL) || (c->config == NULL) || (c->config->tx_buffer == NULL)) {
        return UDS_ERR_NOT_INIT;
    }
    if ((len > 0u) && (data == NULL)) {
        return UDS_ERR_INVALID_ARG;
    }
    if (((uint32_t) len + 1u) > c->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (c->config->fn_mutex_lock != NULL) {
        c->config->fn_mutex_lock(c->config->mutex_handle);
    }

    c->pending_sid = sid;
    c->cb = cb;

    c->config->tx_buffer[0] = sid;
    if ((data != NULL) && (len > 0u)) {
        memcpy(&c->config->tx_buffer[1], data, len);
    }

    int result = c->config->fn_tp_send(NULL, c->config->tx_buffer, (uint16_t) (len + 1u));

    if (c->config->fn_mutex_unlock != NULL) {
        c->config->fn_mutex_unlock(c->config->mutex_handle);
    }
    return result;
}

bool uds_client_handle_response(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len)
{
    if ((c == NULL) || (c->pending_sid == 0u)) {
        return false;
    }
    bool is_pos = (sid == (uint8_t) ((uint16_t) c->pending_sid | UDS_CLIENT_RESPONSE_OFFSET));
    bool is_neg = ((sid == UDS_CLIENT_NEG_RESPONSE_SID) && (len >= 2u) && (data[1] == c->pending_sid));
    if (!is_pos && !is_neg) {
        return false;
    }

    uds_response_cb cb = c->cb;
    c->cb = NULL;
    c->pending_sid = 0u; /* clear before firing so a re-entrant request is not clobbered */
    if (cb != NULL) {
        cb(c, sid, &data[1], (uint16_t) (len - 1u));
    }
    return true;
}
```

- [ ] **Step 3: Add the source to `CMakeLists.txt`** — after `src/services/uds_dtc_store.c` (line ~32) in the `LIBUDS_SOURCES` list:

```cmake
    src/services/uds_dtc_store.c
    src/services/uds_client.c
    src/transport/uds_tp_isotp.c
```

- [ ] **Step 4: Remove the client sub-struct from `include/uds/uds_config.h`** — delete the `uds_client_state_t` typedef and the `uds_client_state_t client;` member of `uds_ctx_t`. (Grep `uds_client_state` / `client;` to find both.)

- [ ] **Step 5: Remove client decls from `include/uds/uds_core.h`** — delete the `uds_response_cb` typedef (the `typedef void (*uds_response_cb)(uds_ctx_t *ctx, ...)` line) and the `uds_client_request(...)` prototype.

- [ ] **Step 6: Remove client code from `src/core/uds_core.c`** — delete the whole `uds_client_request(...)` function, and the response-intercept block in `uds_input_sdu_addr` (the `/* 2. Response to our previous request? (Client Mode) */` `if (ctx->client.pending_sid != 0u) { ... }` block). Renumber the surrounding comment steps if present.

- [ ] **Step 7: Build the library**

```bash
rm -rf build && cmake -S . -B build -DBUILD_TESTING=ON >/dev/null 2>&1
cmake --build build --target uds 2>&1 | grep -iE "warning|error"
```
Expected: no output (library compiles, 0 warnings). Tests will NOT compile yet (consumers use the old API) — that is Tasks 2-3.

- [ ] **Step 8: Commit**

```bash
git add include/uds/uds_client.h src/services/uds_client.c CMakeLists.txt include/uds/uds_config.h include/uds/uds_core.h src/core/uds_core.c
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com commit -m "refactor(client): extract client role into uds_client_ctx_t / uds_client.{h,c}"
```

---

### Task 2: Migrate `test_role_isolation` + add `test_client.c`

**Files:**
- Modify: `tests/unit/test_role_isolation.c`, `tests/CMakeLists.txt`
- Create: `tests/unit/test_client.c`

**Interfaces:**
- Consumes: `uds_client_ctx_t`, `uds_client_request`, `uds_client_handle_response` from Task 1.

- [ ] **Step 1: Rewrite `tests/unit/test_role_isolation.c`** so the client side uses `uds_client_ctx_t`. The original asserted a server async response is not mistaken for a client response while both share one `uds_ctx_t`; with separate types this is structural. Keep the server async assertions (`ctx.server.p2_msg_pending`) unchanged; replace any `uds_client_request(&ctx,...)` / client-field access with a separate `uds_client_ctx_t client = { .config = &cfg };` and route the response through `uds_client_handle_response(&client, ...)`. Run `git show HEAD:tests/unit/test_role_isolation.c` to see the exact assertions to preserve.

- [ ] **Step 2: Create `tests/unit/test_client.c`** — exercise the client module over a mock transport. Use the existing `mock_get_time`/`g_tx_buf` helpers only if needed; the client needs `fn_tp_send` capturing the sent frame.

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "test_helpers.h"
#include "uds/uds_client.h"

static uint8_t g_sent[64];
static uint16_t g_sent_len;
static int client_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx; /* client transports get ctx == NULL */
    for (uint16_t i = 0u; (i < len) && (i < sizeof(g_sent)); i++) g_sent[i] = data[i];
    g_sent_len = len;
    return 0;
}

static int g_cb_calls;
static uint8_t g_cb_sid;
static uint8_t g_cb_payload0;
static void client_cb(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) c;
    g_cb_calls++;
    g_cb_sid = sid;
    g_cb_payload0 = (len > 0u) ? data[0] : 0xFFu;
}

static void make_client(uds_client_ctx_t *c, uds_config_t *cfg, uint8_t *txbuf, uint16_t txsz)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->tx_buffer = txbuf;
    cfg->tx_buffer_size = txsz;
    cfg->fn_tp_send = client_send;
    memset(c, 0, sizeof(*c));
    c->config = cfg;
    g_sent_len = 0u;
    g_cb_calls = 0;
}

static void test_client_request_frames_and_sends(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));

    uint8_t payload[] = {0xF1, 0x90};
    int rc = uds_client_request(&c, 0x22, payload, sizeof(payload), client_cb);
    assert_int_equal(rc, 0);
    assert_int_equal(g_sent_len, 3u);       /* SID + 2 */
    assert_int_equal(g_sent[0], 0x22u);
    assert_int_equal(g_sent[1], 0xF1u);
    assert_int_equal(g_sent[2], 0x90u);
    assert_int_equal(c.pending_sid, 0x22u);
}

static void test_client_positive_response_fires_cb(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* positive response 0x62 <payload> */
    uint8_t resp[] = {0x62, 0xAB};
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_true(consumed);
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(g_cb_sid, 0x62u);
    assert_int_equal(g_cb_payload0, 0xABu); /* payload after SID */
    assert_int_equal(c.pending_sid, 0u);    /* cleared */
}

static void test_client_nrc_response_fires_cb(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* negative response 7F 22 31 (requestOutOfRange) */
    uint8_t resp[] = {0x7F, 0x22, 0x31};
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_true(consumed);
    assert_int_equal(g_cb_calls, 1);
    assert_int_equal(c.pending_sid, 0u);
}

static void test_client_non_matching_frame_not_consumed(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);

    /* an unrelated positive (0x50, a 0x10 response) must NOT be consumed */
    uint8_t other[] = {0x50, 0x01};
    bool consumed = uds_client_handle_response(&c, other[0], other, sizeof(other));
    assert_false(consumed);
    assert_int_equal(g_cb_calls, 0);
    assert_int_equal(c.pending_sid, 0x22u); /* still pending */
}

static void test_client_second_response_after_done_not_consumed(void **state)
{
    (void) state;
    uds_client_ctx_t c;
    uds_config_t cfg;
    uint8_t tx[32];
    make_client(&c, &cfg, tx, sizeof(tx));
    (void) uds_client_request(&c, 0x22, NULL, 0u, client_cb);
    uint8_t resp[] = {0x62, 0xAB};
    (void) uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    /* a duplicate response after completion is ignored */
    bool consumed = uds_client_handle_response(&c, resp[0], resp, sizeof(resp));
    assert_false(consumed);
    assert_int_equal(g_cb_calls, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_client_request_frames_and_sends),
        cmocka_unit_test(test_client_positive_response_fires_cb),
        cmocka_unit_test(test_client_nrc_response_fires_cb),
        cmocka_unit_test(test_client_non_matching_frame_not_consumed),
        cmocka_unit_test(test_client_second_response_after_done_not_consumed),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

- [ ] **Step 3: Register `test_client` in `tests/CMakeLists.txt`** — follow the existing per-test pattern (e.g. how `test_role_isolation` is registered, typically `add_uds_test(test_client unit/test_client.c)`).

- [ ] **Step 4: Build + run the full unit suite**

```bash
cmake --build build --clean-first 2>&1 | grep -iE "warning|error"
ctest --test-dir build 2>&1 | grep -E "tests passed|failed"
```
Expected: 0 warnings; all pass (was 59; +1 new test executable = 60). If a pre-existing server test now fails on a value, STOP — the server lost behaviour it should keep.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/test_role_isolation.c tests/unit/test_client.c tests/CMakeLists.txt
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com commit -m "test(client): client-module unit tests; role-isolation uses separate client ctx"
```

---

### Task 3: Migrate examples + CHANGELOG + full gate

**Files:**
- Modify: `examples/client_demo/main.c`, `examples/h5_uds_tester/firmware/main.c`, `CHANGELOG.md`

**Interfaces:**
- Consumes: `uds_client_ctx_t`, `uds_client_request`, `uds_client_handle_response`.

- [ ] **Step 1: Migrate `examples/client_demo/main.c`** — replace the server `uds_ctx_t` used for client requests with a `uds_client_ctx_t client = { .config = &cfg };`; change `uds_client_request(&ctx, ...)` → `uds_client_request(&client, ...)`; change `on_response`'s first param to `uds_client_ctx_t *`; and route the received frame through `uds_client_handle_response(&client, frame[0], frame, frame_len)` instead of `uds_input_sdu(&ctx, ...)`. Read the current file first (`git show HEAD:examples/client_demo/main.c`) to map the socket-recv path.

- [ ] **Step 2: Migrate `examples/h5_uds_tester/firmware/main.c`** the same way (it is a pure tester: `uds_client_ctx_t` + `uds_client_request` + `uds_client_handle_response`; update the response callback signature). Read it first.

- [ ] **Step 3: Update `CHANGELOG.md`** — under `## [2.0.0]` `### Breaking`, add:

```markdown
- **Client role moved to `uds_client_ctx_t`** (`uds/uds_client.h`): the server
  `uds_ctx_t` no longer carries client state or auto-routes responses.
  `uds_client_request` takes a `uds_client_ctx_t*`; feed incoming responses to
  `uds_client_handle_response` (returns true if consumed). `uds_response_cb`'s
  first argument is now `uds_client_ctx_t*`.
```

- [ ] **Step 4: Build + run host examples** (client_demo is build-only; h5 tester is embedded — build what the host gate covers)

```bash
./scripts/docker_run.sh bash -c '
  set -e
  make -C examples/client_demo clean && make -C examples/client_demo'
```
Expected: builds clean. (h5_uds_tester builds via its embedded pipeline; verify it at least compiles against the new client API if a host build exists, else note it.)

- [ ] **Step 5: Full Docker gate**

```bash
./scripts/docker_run.sh bash -c "cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression --suppress=checkersReport --inconclusive --inline-suppr --error-exitcode=1 -I include -I src/core src/ && find src include examples -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
./scripts/docker_run.sh ./scripts/check_misra.sh
```
Expected: cppcheck clean, clang-format clean, MISRA 3/3 PASSED. If clang-format reflows the new files, apply and re-commit.

- [ ] **Step 6: Re-run all host examples** (the nightly command set) to confirm nothing else broke; clean stray binaries after.

- [ ] **Step 7: Commit**

```bash
git add examples/ CHANGELOG.md
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com commit -m "refactor(client): migrate examples to uds_client_ctx_t; changelog"
```

---

## Notes for the implementer

- Task 1 is the atomic core: the library compiles but the test/example consumers do not until Tasks 2-3. Do not expect a green suite between Task 1 and Task 2.
- The `0x7F` negative-response SID equals the value of the internal `UDS_NRC_SERVICE_NOT_SUPP_IN_SESS` macro by coincidence; the client module uses the literal `0x7Fu` (negative response SID) and must not depend on `src/core/uds_internal.h`.
- Re-sync develop (`git fetch` + `git merge origin/develop`) before the final PR.
