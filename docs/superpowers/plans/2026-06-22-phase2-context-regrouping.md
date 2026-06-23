# Phase 2 — context regrouping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Group `uds_ctx_t`'s runtime fields into five named sub-structs
(`session`, `security`, `server`, `client`, `scratch`) by lifetime/role, with
byte-identical wire behaviour.

**Architecture:** Define the five sub-struct types in `include/uds/uds_config.h`,
rewrite `struct uds_ctx` to contain them, then migrate every `ctx->FIELD`
(src) and `ctx.FIELD` / `g_ctx.FIELD` (tests) accessor to `…->GROUP.FIELD`. The
field-name set is disjoint from config/local names and is accessed only via
`ctx->` in src, so accessor-anchored renames are safe; the compiler flags any
miss. Add a defense-in-depth `scratch` reset at the single top-level entry.

**Tech Stack:** C11, cmocka unit tests, CMake, Docker CI (Ubuntu 24.04,
clang-format-18, cppcheck, MISRA addon), example Makefiles.

## Global Constraints

- **Byte-identical wire behaviour.** No test's expected output bytes may change.
  If a test would need new expected bytes, that is a defect — stop and
  investigate; do NOT edit the expectation. (Same rule as Phase 1.)
- **No public-contract change.** `uds_ok`/`uds_nrc`/`uds_pending`/`uds_none`,
  the handler signature, and `uds_config_t` (including `tx_buffer`) are
  untouched. Only the internal `uds_ctx_t` field layout changes.
- **`ctx->config` is NOT regrouped** (307 uses) — it stays a top-level member.
- **Gate at every commit:** full ctest suite green, 0 build warnings,
  clang-format-18 clean, cppcheck clean, MISRA clean (`scripts/check_misra.sh`).
  Final commit additionally: all host examples build + run
  (the `nightly-examples` command set).
- Commits authored as `w1ne` (`14119286+w1ne@users.noreply.github.com`); no
  Claude/AI/assistant references in any commit message.
- Same unreleased v2.0.0 — no version bump.

## Migration map (old `uds_ctx_t` field → new accessor)

De-prefix rule: drop a prefix only where the sub-struct name makes it
redundant (`security_*`→`security.*`, `server_pending_sid`→`server.pending_sid`,
`client_*`→`client.*`, `active_session`→`session.active`). All other fields
keep their name under the group.

| Old field | New |
|---|---|
| `active_session` | `session.active` |
| `last_msg_time` | `session.last_msg_time` |
| `p2_ms` | `session.p2_ms` |
| `p2_star_ms` | `session.p2_star_ms` |
| `comm_state` | `session.comm_state` |
| `security_level` | `security.level` |
| `authenticated` | `security.authenticated` |
| `security_attempts` | `security.attempts` |
| `security_delay_end` | `security.delay_end` |
| `security_seed_level` | `security.seed_level` |
| `security_seed_len` | `security.seed_len` |
| `security_seed` | `security.seed` |
| `p2_timer_start` | `server.p2_timer_start` |
| `p2_msg_pending` | `server.p2_msg_pending` |
| `p2_star_active` | `server.p2_star_active` |
| `server_pending_sid` | `server.pending_sid` |
| `rcrrp_count` | `server.rcrrp_count` |
| `flash_sequence` | `server.flash_sequence` |
| `link_ctrl_verified` | `server.link_ctrl_verified` |
| `link_ctrl_param` | `server.link_ctrl_param` |
| `periodic_ids` | `server.periodic_ids` |
| `periodic_rates` | `server.periodic_rates` |
| `periodic_timers` | `server.periodic_timers` |
| `periodic_count` | `server.periodic_count` |
| `roe` | `server.roe` |
| `client_cb` | `client.cb` |
| `client_pending_sid` | `client.pending_sid` |
| `suppress_pos_resp` | `scratch.suppress_pos_resp` |
| `req_addr_mode` | `scratch.req_addr_mode` |
| `reset_pending` | `scratch.reset_pending` |
| `reset_pending_type` | `scratch.reset_pending_type` |
| `in_secured_session` | `scratch.in_secured_session` |
| `secure_capturing` | `scratch.secure_capturing` |
| `secure_capture_buf` | `scratch.secure_capture_buf` |
| `secure_capture_size` | `scratch.secure_capture_size` |
| `secure_capture_len` | `scratch.secure_capture_len` |
| `secure_capture_overflow` | `scratch.secure_capture_overflow` |

**Collision-safety:** apply each rename anchored on the accessor and with a
trailing word boundary, so `reset_pending` does not match `reset_pending_type`,
`p2_ms` does not match `p2_msg_pending`/`p2_star_ms`, and `security_seed` does
not match `security_seed_level`/`security_seed_len`. The accessor regex covers
`ctx->`, `ctx.`, and `g_ctx.` while preserving the leading `g_` (see Task 2's
sed). The build is the backstop: any missed or over-matched field is a compile
error (unknown member / `g_session` etc.).

---

### Task 1: Sub-struct types, struct rewrite, and src/ migration

**Files:**
- Modify: `include/uds/uds_config.h` (struct `uds_ctx`, lines 706-806)
- Modify: `src/core/uds_core.c`, `src/services/*.c` (10 files),
  `src/services/uds_dtc_store.c` — every `ctx->FIELD` in the map
- Modify: `src/transport/uds_tp_isotp.c` (only if it accesses any mapped field;
  current grep shows none — verify)

**Interfaces:**
- Produces: the five sub-struct types and the new `uds_ctx_t` layout consumed by
  Tasks 2-3 and all callers.

- [ ] **Step 1: Replace the `struct uds_ctx` body** (`include/uds/uds_config.h`,
  the `typedef struct uds_ctx { … } uds_ctx_t;` at lines 706-806) with the
  grouped form. Keep `config` top-level; keep the `#if (UDS_ROE_MAX_EVENTS > 0)`
  guard around `roe` inside `server`:

```c
typedef struct uds_session_state
{
    uint8_t active;          /**< Currently active session */
    uint32_t last_msg_time;  /**< Last valid message time (S3 timer) */
    uint16_t p2_ms;          /**< Current P2 server timeout */
    uint32_t p2_star_ms;     /**< Current P2* server timeout */
    uint8_t comm_state;      /**< CommunicationControl (0x28) state */
} uds_session_state_t;

typedef struct uds_security_state
{
    uint8_t level;                       /**< Current security level (0=Locked) */
    bool authenticated;                  /**< 0x29 authentication state */
    uint32_t delay_end;                  /**< Timestamp when delay expires */
    uint8_t attempts;                    /**< Failed-attempt counter */
    uint8_t seed_level;                  /**< Level a seed is outstanding for */
    uint8_t seed_len;                    /**< Length of cached seed */
    uint8_t seed[UDS_SECURITY_SEED_MAX]; /**< Last issued seed */
} uds_security_state_t;

typedef struct uds_server_state
{
    uint32_t p2_timer_start;     /**< P2 tracking start */
    bool p2_msg_pending;         /**< A service returned UDS_PENDING */
    bool p2_star_active;         /**< First 0x78 already sent */
    uint8_t pending_sid;         /**< SID awaiting an async (0x78) response */
    uint16_t rcrrp_count;        /**< NRC 0x78 repetition counter (C-07) */
    uint8_t flash_sequence;      /**< Block Sequence Counter (0x36) */
    bool link_ctrl_verified;     /**< 0x87 verify accepted */
    uint32_t link_ctrl_param;    /**< 0x87 latched link parameter */
    uint8_t periodic_ids[8];     /**< Active periodic IDs (0x2A) */
    uint8_t periodic_rates[8];   /**< Periodic rates (1-3) */
    uint32_t periodic_timers[8]; /**< Periodic deadlines */
    uint8_t periodic_count;      /**< Active periodic count */
#if (UDS_ROE_MAX_EVENTS > 0)
    uds_roe_slot_t roe[UDS_ROE_MAX_EVENTS]; /**< ResponseOnEvent slots (0x86) */
#endif
} uds_server_state_t;

typedef struct uds_client_state
{
    void *cb;            /**< Callback for the awaited response */
    uint8_t pending_sid; /**< SID we are awaiting a response for (0=none) */
} uds_client_state_t;

typedef struct uds_dispatch_scratch
{
    bool suppress_pos_resp;       /**< Centralized suppressPosRsp (bit 7) */
    uint8_t req_addr_mode;        /**< Addressing mode of the request in flight */
    bool reset_pending;           /**< 0x11: fn_reset runs AFTER emit */
    uint8_t reset_pending_type;   /**< resetType for the deferred reset */
    bool in_secured_session;      /**< Dispatching an unwrapped 0x84 inner request */
    bool secure_capturing;        /**< Capturing the inner response */
    uint8_t *secure_capture_buf;  /**< Capture target (caller stack) */
    uint16_t secure_capture_size; /**< Capacity of secure_capture_buf */
    uint16_t secure_capture_len;  /**< Bytes captured */
    bool secure_capture_overflow; /**< Inner response exceeded the buffer */
} uds_dispatch_scratch_t;

typedef struct uds_ctx
{
    const uds_config_t *config; /**< Config pointer (must remain valid) */

    uds_session_state_t session;
    uds_security_state_t security;
    uds_server_state_t server;
    uds_client_state_t client;
    uds_dispatch_scratch_t scratch;
} uds_ctx_t;
```

- [ ] **Step 2: Migrate all `src/` accessors** using the map. Run, from the repo
  root, one sed invocation per old-field row over the src files, anchored on
  `ctx->` with a trailing word boundary. Example for the first three rows
  (apply the full table the same way):

```bash
SRC="src/core/uds_core.c src/services/*.c src/transport/uds_tp_isotp.c"
sed -i -E 's/\bctx->active_session\b/ctx->session.active/g' $SRC
sed -i -E 's/\bctx->last_msg_time\b/ctx->session.last_msg_time/g' $SRC
sed -i -E 's/\bctx->security_seed_level\b/ctx->security.seed_level/g' $SRC
sed -i -E 's/\bctx->security_seed_len\b/ctx->security.seed_len/g' $SRC
sed -i -E 's/\bctx->security_seed\b/ctx->security.seed/g' $SRC   # after _level/_len
# …one line per remaining row of the migration map…
```

  Order `security_seed_level`/`security_seed_len` BEFORE `security_seed`, and
  `reset_pending_type` BEFORE `reset_pending`, so the trailing `\b` resolves
  correctly. (With `\b` the order is belt-and-suspenders, but keep it.)

- [ ] **Step 3: Build the library** and resolve any residual compile errors
  (a missed accessor = unknown member; an over-match = e.g. `g_session`):

```bash
cmake --build build --clean-first 2>&1 | grep -iE "warning|error"
```
  Expected: no output (0 warnings, library compiles). Tests will NOT yet compile
  — that is Task 2.

- [ ] **Step 4: Add the top-level scratch reset.** In `src/core/uds_core.c`, at
  the start of the single top-level entry `uds_input_sdu_addr` (the function
  `uds_input_sdu` wraps), before the address mode is set and before
  `handle_request`, zero the per-dispatch scratch so no stale flag from a prior
  top-level request can survive:

```c
/* Defense-in-depth: a fresh top-level request starts with clean per-dispatch
 * scratch. Not done in handle_request (the 0x84 inner dispatch runs there and
 * must keep the outer's capture state); the inner request's suppress bit is
 * still cleared per-dispatch inside handle_request as before. */
memset(&ctx->scratch, 0, sizeof ctx->scratch);
```
  Keep the existing per-dispatch `scratch.suppress_pos_resp` clear inside
  `handle_request` (needed by the nested 0x84 inner dispatch). Set
  `scratch.req_addr_mode` from the address argument AFTER this memset, exactly
  as before. Confirm `<string.h>` is included (it is, for `memcpy`).

- [ ] **Step 5: Commit**

```bash
git add include/uds/uds_config.h src/
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com \
  commit -m "refactor(ctx): group uds_ctx_t into session/security/server/client/scratch"
```

---

### Task 2: Migrate tests and examples

**Files:**
- Modify: every `tests/unit/*.c` accessing a mapped field via `ctx.` / `g_ctx.`
  (~30 files)
- Modify: any `examples/*/main.c` accessing a mapped field directly (audit
  first; most examples use callbacks, not raw ctx fields)

**Interfaces:**
- Consumes: the `uds_ctx_t` layout from Task 1.

- [ ] **Step 1: Audit example direct field access**

```bash
grep -rlE "\.(active_session|security_level|security_|p2_msg_pending|periodic_|secure_capture|suppress_pos_resp|reset_pending|flash_sequence|comm_state|authenticated|in_secured_session|link_ctrl|rcrrp_count|client_)" examples/
```
  Record the matching files; they get the same sed in Step 2.

- [ ] **Step 2: Migrate `tests/` (and any example files found) accessors.** Same
  map, anchored on `ctx.` / `g_ctx.` while preserving the leading `g_`. Use a
  capture group so both accessor forms are handled in one rule per field:

```bash
TST="tests/unit/*.c <example files from Step 1>"
# \1 preserves whichever accessor matched (g_ctx. before ctx. so g_ is kept).
sed -i -E 's/\b(g_ctx\.|ctx\.)active_session\b/\1session.active/g' $TST
sed -i -E 's/\b(g_ctx\.|ctx\.)security_seed_level\b/\1security.seed_level/g' $TST
sed -i -E 's/\b(g_ctx\.|ctx\.)security_seed_len\b/\1security.seed_len/g' $TST
sed -i -E 's/\b(g_ctx\.|ctx\.)security_seed\b/\1security.seed/g' $TST
# …one line per remaining row, same ordering caveats as Task 1 Step 2…
```

- [ ] **Step 3: Build + run the full suite**

```bash
cmake --build build --clean-first 2>&1 | grep -iE "warning|error"   # expect none
ctest --test-dir build 2>&1 | grep -E "tests passed|failed"          # expect 68/68
```
  Expected: 0 warnings, `100% tests passed … out of 68`. Byte-identical guard:
  no test required an expected-bytes change. If any test fails on a value
  mismatch (not a compile error), STOP — that means the regroup changed
  behaviour (a defect), do not edit the expectation.

- [ ] **Step 4: Commit**

```bash
git add tests/ examples/
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com \
  commit -m "refactor(ctx): migrate tests and examples to grouped context fields"
```

---

### Task 3: Scratch-leak regression test, docs, and full gate

**Files:**
- Modify: `tests/unit/test_nrc_priority_a3.c` (add one test) or
  `tests/unit/test_core_coverage.c` — wherever a `uds_ctx_t` is directly
  constructed
- Modify: `CHANGELOG.md` (extend the 2.0.0 Breaking note)

**Interfaces:**
- Consumes: the `scratch` reset from Task 1 Step 4.

- [ ] **Step 1: Write the failing test** — prove a stale scratch flag set before
  a fresh top-level request does not affect it. Add to `test_nrc_priority_a3.c`
  (it already builds a `uds_ctx_t` and registers user services). Use the
  existing harness (`setup_ctx`, `mock_get_time`, `mock_tp_send`, `g_tx_buf`):

```c
static void test_scratch_does_not_leak_into_next_request(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);

    /* Simulate a stale per-dispatch flag left over from a prior request. */
    ctx.scratch.reset_pending = true;
    ctx.scratch.reset_pending_type = 0x01u;
    ctx.scratch.suppress_pos_resp = true;

    /* A fresh top-level request (0x3E TesterPresent, sub=0x00) must respond
     * normally: the stale suppress must NOT silence it, and the stale
     * reset_pending must NOT trigger fn_reset. */
    bool reset_called = false;
    cfg.fn_reset = NULL; /* if reset fired, server.pending state would be wrong */
    uint8_t req[] = {0x3Eu, 0x00u};
    will_return(mock_get_time, 1000u);
    will_return(mock_get_time, 1000u);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2u); /* 7E 00 */
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(g_tx_buf[0], 0x7Eu);
    assert_int_equal(g_tx_buf[1], 0x00u);
    assert_false(ctx.scratch.reset_pending);
    assert_false(ctx.scratch.suppress_pos_resp);
    (void) reset_called;
}
```
  Register it in the file's `CMUnitTest` array.

- [ ] **Step 2: Run it to confirm it passes against Task 1's reset**

```bash
ctest --test-dir build -R test_nrc_priority_a3 --output-on-failure
```
  Expected: PASS. (If it fails, the Task 1 Step 4 reset is wrong — fix the reset,
  not the test.)

- [ ] **Step 3: Extend the CHANGELOG 2.0.0 Breaking note** — add one bullet:

```markdown
- **Internal context fields regrouped into sub-structs** (not API): the runtime
  `uds_ctx_t` now groups its fields into `session`, `security`, `server`,
  `client`, and `scratch` sub-structs (e.g. `ctx->active_session` →
  `ctx->session.active`, `ctx->security_level` → `ctx->security.level`). The
  field layout was never part of the API contract; this affects only code that
  read `uds_ctx_t` fields directly.
```

- [ ] **Step 4: Full local gate**

```bash
cmake --build build --clean-first 2>&1 | grep -iE "warning|error"  # none
ctest --test-dir build 2>&1 | grep -E "tests passed|failed"        # 68/68
```

- [ ] **Step 5: Full Docker CI gate (the real one)**

```bash
./scripts/docker_run.sh bash -c "cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression --suppress=checkersReport --inconclusive --inline-suppr --error-exitcode=1 -I include -I src/core src/ && find src include examples -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
./scripts/docker_run.sh ./scripts/check_misra.sh
```
  Expected: cppcheck clean, clang-format clean, MISRA 3/3 PASSED. If clang-format
  reflows the new sub-struct definitions, apply its formatting and re-commit.

- [ ] **Step 6: Examples build + run gate** (the nightly command set, run now)

```bash
./scripts/docker_run.sh bash -c '
  set -e
  for ex in auth_challenge custom_service dtc_clear dtc_full_coverage dtc_store; do
    make -C examples/$ex clean; make -C examples/$ex run; done
  for ex in client_demo host_sim; do make -C examples/$ex clean; make -C examples/$ex; done
  for ex in auth_challenge_mbedtls security_access_mbedtls; do for c in mbedtls wolfssl; do
    make -C examples/$ex clean; make -C examples/$ex CRYPTO=$c; examples/$ex/uds_$ex >/dev/null; done; done'
```
  Expected: every example builds and runs (exit 0). Clean stray binaries
  afterward (`.gitignore` already covers them).

- [ ] **Step 7: Commit**

```bash
git add tests/ CHANGELOG.md
git -c user.name=w1ne -c user.email=14119286+w1ne@users.noreply.github.com \
  commit -m "test(ctx): lock scratch non-leak; document context regrouping"
```

---

## Notes for the implementer

- This is one logically-atomic rename split across three commits only for review
  granularity; the library does not fully build until Task 1 completes, and the
  test suite does not build until Task 2 completes. Do not expect a green suite
  between Task 1 and Task 2.
- The compiler is the rename's safety net: an unknown-member error means a missed
  accessor; a `g_session`/`session.active` outside `ctx` means an over-match.
  Both are deterministic to fix.
- Do not touch `ctx->config` or any `uds_config_t` field. Do not rename bare
  field tokens in comments or unrelated locals — the accessor anchor prevents it.
