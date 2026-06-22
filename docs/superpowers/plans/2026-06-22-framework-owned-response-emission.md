# Framework-Owned Response Emission — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move UDS response emission, positive-response suppression, and post-response side-effect ordering out of the 27 individual service handlers and into the dispatch framework, removing the bug class behind #76/#80.

**Architecture:** Handlers stop calling `uds_send_response()`/`uds_send_nrc()` and instead describe their response via an out-parameter `uds_result_t` (POSITIVE/NRC/PENDING). `execute_handler()` becomes the single emitter: it applies suppression once, emits, then runs a deferred reset (the only post-emit side-effect). A temporary second handler column (`handler_v2`) lets each service file migrate and be verified green independently; a final task removes the legacy column and flips the public `user_services` typedef (the v2.0.0 break).

**Tech Stack:** C11, CMake + cmocka unit tests, clang-format 18 (CI Docker image), GitHub Actions `core-gate`.

## Global Constraints

- **Behaviour-preserving:** no test's *expected output bytes* may change. If a test would need new expected bytes, that is a refactor defect — STOP and investigate; do not edit the expectation.
- **Gate after every commit:** full suite `ctest --test-dir build` = 59/59 (plus the new tests) pass, AND `clang-format --dry-run --Werror` over `src include examples` is clean. Validate clang-format in the CI Docker image (`./scripts/docker_run.sh`), local clang-format is v14 and disagrees with CI's v18.
- **Worktree:** all work happens in `/tmp/udslib-refactor` on branch `refactor/phase1-response-builder` (already created off `origin/develop`).
- **Commits:** author `w1ne <14119286+w1ne@users.noreply.github.com>`; no Claude/AI references in messages.
- **Canaries:** `test_ecu_reset_response_sent_before_reset` (#76) and `test_ecu_reset_suppress_does_not_leak` (#80) must stay green at every step.
- **Three result kinds only:** `UDS_RESULT_POSITIVE`, `UDS_RESULT_NRC`, `UDS_RESULT_PENDING`. No NONE, no after-action enum.

---

## Migration Recipe (shared by Tasks 3–12)

Every per-file migration task applies this exact transformation to each table handler defined in the file. Read it once; each task below lists only its file's handlers, table-entry line(s), and special cases.

**Signature.** Change the handler definition and its declaration in `src/core/uds_internal.h`:

```c
/* before */ int  uds_internal_handle_X(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
/* after  */ void uds_internal_handle_X(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                        uds_result_t *out);
```

**Body — replace every emission/return with an `*out` assignment (no early returns that emit):**

| before | after |
|---|---|
| `return uds_send_nrc(ctx, SID, NRC);` | `uds_nrc(out, NRC); return;` |
| `return uds_send_response(ctx, n);` | `uds_ok(out, n); return;` |
| `if (ctx->suppress_pos_resp) { ...; return UDS_OK; }` | **delete** — suppression is now central; let the code build the payload and fall through to `uds_ok(out, n)` |
| `return UDS_PENDING;` | `uds_pending(out); return;` |
| `return UDS_OK;` *(after a send already converted)* | already covered by the `uds_ok(...)` above; delete the stray `return UDS_OK;` |
| internal helper that itself called `uds_send_*` and returned int | give the helper an `uds_result_t *out` param too and apply the same table |

**Table entry** in `src/core/uds_core.c` `core_services[]`: move the function pointer from the `handler` column to the new trailing `handler_v2` column, setting `handler` to `NULL`. (Line numbers cited in tasks are indicative against current `develop` and drift as edits land — always match by `UDS_SID_*` constant and handler name, not by line.)

```c
/* before */ {UDS_SID_X, min, sess, sec, uds_internal_handle_X, sub_mask, addr},
/* after  */ {UDS_SID_X, min, sess, sec, NULL, sub_mask, addr, uds_internal_handle_X},
```

**Do NOT** delete the suppress consumption, secure-capture, or `fn_tp_send` logic — those now live solely in `execute_handler`/`uds_emit_response` (Tasks 1–2). A migrated handler never touches `suppress_pos_resp` and never calls `uds_send_*`.

**Per-file gate (end of every migration task):**
```bash
cmake --build build 2>&1 | tail -3            # compiles (legacy + v2 columns coexist)
ctest --test-dir build                         # all pass, byte-identical
./scripts/docker_run.sh bash -c "find src include examples -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
```
Then commit. A file is not committed until its suite run is green.

---

## Task 0: Close the suppress/ordering test gap (no production change)

**Files:**
- Test: `tests/unit/test_compliance_suppress.c` (extend) — table-driven suppress assertion for every sub-function SID
- Test: `tests/unit/test_service_10.c` (extend) — assert `fn_nvm_save` is invoked on a session change
- Test: `tests/CMakeLists.txt` (only if a new test file is added; extending existing files needs no change)

**Interfaces:**
- Produces: a verified gate proving (a) every sub-function service suppresses its positive response under bit 7, and (b) session control persists state via `fn_nvm_save`. These behaviours must remain byte-identical through Tasks 3–13.

- [ ] **Step 1: Inventory current suppress coverage**

Run: `grep -l "suppress" tests/unit/*.c` and read `tests/unit/test_compliance_suppress.c`. List which sub-function SIDs (0x10, 0x11, 0x19, 0x28, 0x2C, 0x27, 0x29, 0x31, 0x83, 0x85, 0x87) already have a suppress assertion and which do not.

- [ ] **Step 2: Write a failing suppress test for each uncovered sub-function SID**

For each uncovered SID, add a case asserting that a request with bit 7 set on the sub-function produces **no** `fn_tp_send` call. Pattern (adapt SID/sub/min-length/preconditions per service):

```c
static void test_suppress_<sid>(void **state)
{
    (void) state;
    uds_ctx_t ctx; uds_config_t cfg; setup_ctx(&ctx, &cfg);
    /* wire any callback the service needs to reach a positive path */
    uint8_t req[] = {0x<sid>, 0x80 | 0x<sub>, /* ...min payload... */};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): a positive response MUST be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
    /* reaching here without a cmocka "unexpected mock_tp_send" failure proves suppression */
}
```

- [ ] **Step 3: Write a failing test that session change persists state**

In `tests/unit/test_service_10.c`, add a mock `fn_nvm_save` that sets a counter, send `0x10 0x03` (extended), and assert the counter incremented and the two state bytes are `{active_session, security_level}`:

```c
static int g_nvm_saved; static uint8_t g_nvm_state[2];
static int mock_nvm_save(uds_ctx_t *c, const uint8_t *s, uint16_t n)
{ (void) c; if (n >= 2u) { g_nvm_state[0] = s[0]; g_nvm_state[1] = s[1]; } g_nvm_saved++; return 0; }

static void test_session_change_persists_state(void **state)
{
    (void) state;
    uds_ctx_t ctx; uds_config_t cfg; setup_ctx(&ctx, &cfg);
    cfg.fn_nvm_save = mock_nvm_save; g_nvm_saved = 0;
    uint8_t req[] = {0x10, 0x03};
    will_return(mock_get_time, 1000); will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data); expect_value(mock_tp_send, len, 6); will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(g_nvm_saved, 1);
    assert_int_equal(g_nvm_state[0], 0x03); /* extended session active */
}
```

- [ ] **Step 4: Register new test functions in their `main()` arrays, build, run**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
Expected: all new tests PASS on current `develop` (they document existing behaviour). If any FAIL, that is a pre-existing bug — STOP and report, do not "fix" by changing expectations.

- [ ] **Step 5: clang-format + commit**

```bash
./scripts/docker_run.sh bash -c "clang-format -i tests/unit/test_compliance_suppress.c tests/unit/test_service_10.c"
git add tests/ && git commit -m "test: cover suppressPosRsp for all sub-function services and session-change persistence"
```

---

## Task 1: Result type, context fields, and emission-helper split

**Files:**
- Modify: `include/uds/uds_config.h` — add `uds_result_kind_t`, `uds_result_t`, inline helpers; extend `uds_ctx` with `reset_pending`/`reset_pending_type`
- Modify: `src/core/uds_core.c:683-716` — split `uds_send_response` into pure `uds_emit_response` + suppress shim
- Modify: `src/core/uds_internal.h` — declare `uds_emit_response`
- Test: `tests/unit/test_service_11.c` (canaries already present — they guard this)

**Interfaces:**
- Produces: `uds_result_t {uds_result_kind_t kind; uint16_t len; uint8_t nrc;}`; helpers `uds_ok(out,len)`, `uds_nrc(out,nrc)`, `uds_pending(out)`; `int uds_emit_response(uds_ctx_t*, uint16_t len)` (pure transport/secure-capture, no suppress); `ctx->reset_pending` (bool), `ctx->reset_pending_type` (uint8_t).

- [ ] **Step 1: Add the result type and helpers to `include/uds/uds_config.h`**

Place above the `uds_service_handler_t` typedef:

```c
/** Outcome a service handler asks the framework to emit. */
typedef enum {
    UDS_RESULT_POSITIVE, /**< tx_buffer[0..len-1] holds the positive response */
    UDS_RESULT_NRC,      /**< emit negative response with .nrc                */
    UDS_RESULT_PENDING   /**< async: framework emits 0x78 and tracks P2*      */
} uds_result_kind_t;

typedef struct {
    uds_result_kind_t kind;
    uint16_t          len; /**< payload length in tx_buffer (POSITIVE) */
    uint8_t           nrc; /**< NRC code (NRC)                         */
} uds_result_t;

static inline void uds_ok(uds_result_t *out, uint16_t len)
{ out->kind = UDS_RESULT_POSITIVE; out->len = len; }
static inline void uds_nrc(uds_result_t *out, uint8_t nrc)
{ out->kind = UDS_RESULT_NRC; out->nrc = nrc; }
static inline void uds_pending(uds_result_t *out)
{ out->kind = UDS_RESULT_PENDING; }
```

- [ ] **Step 2: Add reset fields to `struct uds_ctx`**

In `include/uds/uds_config.h`, in the `/* --- Server role ... */` region of `struct uds_ctx`, add:

```c
    /** Set by the 0x11 handler; framework calls fn_reset AFTER emitting. */
    bool    reset_pending;
    /** resetType to pass to fn_reset when reset_pending is consumed. */
    uint8_t reset_pending_type;
```

- [ ] **Step 3: Split `uds_send_response` (src/core/uds_core.c)**

Replace the body of `uds_send_response` (lines ~683-716) with a pure emitter plus a suppress shim:

```c
int uds_emit_response(uds_ctx_t *ctx, uint16_t len)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }
    if (len > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }
    ctx->p2_msg_pending = false;
    ctx->server_pending_sid = 0u;

    if (ctx->secure_capturing) {
        uint16_t n = (len <= ctx->secure_capture_size) ? len : ctx->secure_capture_size;
        memcpy(ctx->secure_capture_buf, ctx->config->tx_buffer, n);
        ctx->secure_capture_len = n;
        ctx->rcrrp_count = 0u;
        return UDS_OK;
    }
    ctx->rcrrp_count = 0u;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_response(uds_ctx_t *ctx, uint16_t len) /* public compat shim */
{
    if (ctx && ctx->suppress_pos_resp) {
        ctx->suppress_pos_resp = false;
        ctx->rcrrp_count = 0u;
        if (ctx->secure_capturing) {
            ctx->secure_capture_len = 0u;
        }
        ctx->p2_msg_pending = false;
        ctx->server_pending_sid = 0u;
        return UDS_OK;
    }
    return uds_emit_response(ctx, len);
}
```

- [ ] **Step 4: Declare `uds_emit_response` in `src/core/uds_internal.h`**

Add near the other core declarations: `int uds_emit_response(uds_ctx_t *ctx, uint16_t len);`

- [ ] **Step 5: Build and run full suite (behaviour unchanged)**

Run: `cmake --build build && ctest --test-dir build`
Expected: 59/59 + Task 0 tests PASS. The canaries pass. No expected bytes changed.

- [ ] **Step 6: clang-format + commit**

```bash
./scripts/docker_run.sh bash -c "find src include -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
git add include/ src/ && git commit -m "refactor(core): add uds_result_t + split uds_emit_response from suppress shim"
```

---

## Task 2: Dual-path dispatch and post-emit reset

**Files:**
- Modify: `include/uds/uds_config.h` — add trailing `handler_v2` column to `uds_service_entry_t`
- Modify: `src/core/uds_internal.h` — typedef for the v2 handler
- Modify: `src/core/uds_core.c:247-259` — rewrite `execute_handler` to prefer `handler_v2`, emit centrally, consume `reset_pending`
- Modify: `src/core/uds_core.c:447-452` — zero the new ctx fields in `uds_init` (already covered by `memset`, verify)

**Interfaces:**
- Consumes: `uds_result_t`, `uds_emit_response`, `reset_pending` (Task 1).
- Produces: `typedef void (*uds_handler_v2_t)(struct uds_ctx*, const uint8_t*, uint16_t, uds_result_t*)`; `uds_service_entry_t.handler_v2` (last field, NULL = use legacy `handler`); `execute_handler` emits for v2 handlers.

- [ ] **Step 1: Add the v2 handler typedef (src/core/uds_internal.h)**

```c
typedef void (*uds_handler_v2_t)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                                 uds_result_t *out);
```

- [ ] **Step 2: Add `handler_v2` as the LAST field of `uds_service_entry_t`**

```c
    uint8_t address_mode;        /**< Allowed addressing; 0 = both */
    uds_handler_v2_t handler_v2; /**< Migrated handler; NULL => use legacy handler */
} uds_service_entry_t;
```
All existing positional initializers `{...7 fields...}` keep compiling (the new field zero-fills to NULL).

- [ ] **Step 3: Rewrite `execute_handler` (src/core/uds_core.c)**

```c
static void execute_handler(uds_ctx_t *ctx, const uds_service_entry_t *service,
                            const uint8_t *data, uint16_t len)
{
    if (service->handler_v2 != NULL) {
        uds_result_t r;
        r.kind = UDS_RESULT_POSITIVE; r.len = 0u; r.nrc = 0u;
        service->handler_v2(ctx, data, len, &r);

        switch (r.kind) {
        case UDS_RESULT_PENDING:
            uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
            ctx->p2_msg_pending = true;
            ctx->p2_star_active = true;
            ctx->p2_timer_start = ctx->config->get_time_ms();
            ctx->server_pending_sid = data[0];
            break;
        case UDS_RESULT_NRC:
            uds_send_nrc(ctx, data[0], r.nrc); /* NRC never suppressed (ISO) */
            break;
        case UDS_RESULT_POSITIVE:
        default:
            if (ctx->suppress_pos_resp) {
                ctx->suppress_pos_resp = false;
                ctx->rcrrp_count = 0u;
                ctx->p2_msg_pending = false;
                ctx->server_pending_sid = 0u;
                if (ctx->secure_capturing) {
                    ctx->secure_capture_len = 0u;
                }
            } else {
                (void) uds_emit_response(ctx, r.len);
            }
            break;
        }
    } else {
        int res = service->handler(ctx, data, len); /* legacy: emits internally */
        if (res == UDS_PENDING) {
            uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
            ctx->p2_msg_pending = true;
            ctx->p2_star_active = true;
            ctx->p2_timer_start = ctx->config->get_time_ms();
            ctx->server_pending_sid = data[0];
        }
    }

    /* Deferred reset runs only after the response is on the wire. */
    if (ctx->reset_pending) {
        ctx->reset_pending = false;
        if (ctx->config->fn_reset != NULL) {
            ctx->config->fn_reset(ctx, ctx->reset_pending_type);
        }
    }
}
```
Note: `handle_request` ignores the old `int` return, so changing `execute_handler` to `void` is safe; update its call site if the compiler warns.

- [ ] **Step 4: Build and run full suite (no handler migrated yet → legacy path only)**

Run: `cmake --build build && ctest --test-dir build`
Expected: 59/59 + Task 0 tests PASS. `reset_pending` is never set yet, so the new block is a no-op.

- [ ] **Step 5: clang-format + commit**

```bash
./scripts/docker_run.sh bash -c "find src include -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
git add include/ src/ && git commit -m "refactor(core): dual-path dispatch with central emission + post-emit reset"
```

---

## Task 3: Migrate `uds_service_session.c`

**Files:** Modify `src/services/uds_service_session.c` (handlers `uds_internal_handle_session_control`, `uds_internal_handle_tester_present`); `src/core/uds_internal.h` (their decls); `src/core/uds_core.c` table lines 46 and 78.

**Special case:** `session_control` currently calls `uds_send_response(...)` and THEN `fn_nvm_save(...)`. Move the `fn_nvm_save` block to **before** the `uds_ok(out, 6)` assignment (behaviour-preserving — save runs unconditionally either way; Task 0's `test_session_change_persists_state` is the guard).

- [ ] **Step 1:** Apply the Migration Recipe to both handlers; reorder `fn_nvm_save` before the result assignment in `session_control`.
- [ ] **Step 2:** Flip table entries (lines 46, 78) to the `handler_v2` column per the recipe.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS, byte-identical. Run `test_service_10` and `test_service_3E` specifically.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x10/0x3E): migrate session services to result-descriptor contract`.

---

## Task 4: Migrate `uds_service_maintenance.c`

**Files:** Modify `src/services/uds_service_maintenance.c` (handlers: `ecu_reset`, `comm_control`, `clear_dtc`, `read_dtc_info`, `control_dtc_setting`, plus the file-internal helpers `uds_internal_dtc_by_status` and the snapshot/extended-data formatters — give each an `uds_result_t *out`); `src/core/uds_internal.h`; `src/core/uds_core.c` table lines 48, 49, 50, 61, 80.

**Special case (reset, #76/#80):** `ecu_reset` must NOT call `fn_reset`. Instead build the `0x51 sub` response into `tx_buffer`, set `ctx->reset_pending = true; ctx->reset_pending_type = sub;`, then `uds_ok(out, 2)`. On the suppress path, still set `reset_pending` and `uds_ok(out, 2)` — the framework suppresses the emit and runs the reset after. Delete the old `if (suppress_pos_resp) {...} ... fn_reset ... uds_send_response` sequence entirely.

- [ ] **Step 1:** Apply the Migration Recipe to all five handlers and their internal helpers.
- [ ] **Step 2:** Implement the reset special case above.
- [ ] **Step 3:** Flip table entries (48, 49, 50, 61, 80) to `handler_v2`.
- [ ] **Step 4:** Build, run full suite. Expected: all PASS. The canaries `test_ecu_reset_response_sent_before_reset` and `test_ecu_reset_suppress_does_not_leak` MUST pass; also run `test_service_11`, `test_service_28`, `test_service_dtc`, `test_service_did`.
- [ ] **Step 5:** clang-format (Docker) + commit: `refactor(0x11/0x14/0x19/0x28/0x85): migrate maintenance services; reset via reset_pending`.

---

## Task 5: Migrate `uds_service_data.c`

**Files:** Modify `src/services/uds_service_data.c` (handlers: `read_data_by_id`, `read_scaling`, `dynamic_did`, `write_data_by_id`, `periodic_read`); `src/core/uds_internal.h`; `src/core/uds_core.c` table lines 52, 56, 57, 65, 82.

- [ ] **Step 1:** Apply the Migration Recipe to all five handlers (and any file-internal emit helpers).
- [ ] **Step 2:** Flip table entries (52, 56, 57, 65, 82) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_22`, `test_service_24`, `test_service_2C`, `test_service_2E`, `test_service_2A`, `test_service_did`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x22/0x24/0x2A/0x2C/0x2E): migrate data services`.

---

## Task 6: Migrate `uds_service_security.c`

**Files:** Modify `src/services/uds_service_security.c` (handlers: `security_access`, `authentication`); `src/core/uds_internal.h`; `src/core/uds_core.c` table lines 59, 63.

- [ ] **Step 1:** Apply the Migration Recipe to both handlers.
- [ ] **Step 2:** Flip table entries (59, 63) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_27`, `test_service_29`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x27/0x29): migrate security services`.

---

## Task 7: Migrate `uds_service_mem.c`

**Files:** Modify `src/services/uds_service_mem.c` (handlers: `read_memory_by_addr`, `write_memory_by_addr`); `src/core/uds_internal.h`; `src/core/uds_core.c` table lines 54, 76.

- [ ] **Step 1:** Apply the Migration Recipe to both handlers.
- [ ] **Step 2:** Flip table entries (54, 76) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_mem`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x23/0x3D): migrate memory services`.

---

## Task 8: Migrate `uds_service_link.c`

**Files:** Modify `src/services/uds_service_link.c` (handlers: `link_control`, `access_timing`); `src/core/uds_internal.h`; `src/core/uds_core.c` table lines 86, 88.

- [ ] **Step 1:** Apply the Migration Recipe to both handlers.
- [ ] **Step 2:** Flip table entries (86, 88) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_87`, `test_service_83`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x83/0x87): migrate link/timing services`.

---

## Task 9: Migrate `uds_service_io.c`

**Files:** Modify `src/services/uds_service_io.c` (handler: `io_control`); `src/core/uds_internal.h`; `src/core/uds_core.c` table line 84.

- [ ] **Step 1:** Apply the Migration Recipe to the handler.
- [ ] **Step 2:** Flip table entry (84) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_2F`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x2F): migrate IO control service`.

---

## Task 10: Migrate `uds_service_roe.c`

**Files:** Modify `src/services/uds_service_roe.c` (handler: `response_on_event`); `src/core/uds_internal.h`; `src/core/uds_core.c` table line 93.

**Note:** ROE has several `if (suppress_pos_resp) return UDS_OK;` branches across its sub-functions — delete each per the recipe and let the build-then-`uds_ok(out, pos)` path run. ROE also emits asynchronous event responses via `uds_emit_response`/`uds_send_response` from `uds_process`; those are NOT handler returns and stay as-is.

- [ ] **Step 1:** Apply the Migration Recipe to the handler (all sub-function branches).
- [ ] **Step 2:** Flip table entry (93) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_86`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x86): migrate response-on-event service`.

---

## Task 11: Migrate `uds_service_flash.c`

**Files:** Modify `src/services/uds_service_flash.c`; `src/core/uds_internal.h`; `src/core/uds_core.c` (entries for SIDs 0x34, 0x35, 0x36, 0x37, 0x38). The exact handler set is every `^int uds_internal_handle_*` defined in the file — enumerate with `grep -n "^int uds_internal_handle" src/services/uds_service_flash.c` (6 handlers) and migrate each.

- [ ] **Step 1:** Apply the Migration Recipe to every handler the grep lists.
- [ ] **Step 2:** Flip table entries (69, 71, 72, 75, 85) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_34_flex`, `test_service_35`, `test_service_38`, `test_service_flash`, `flash_demo`.
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x34/0x35/0x36/0x37/0x38): migrate flash/transfer services`.

---

## Task 12: Migrate `uds_internal_handle_secured_data` (0x84, in `src/core/uds_core.c`)

**Files:** Modify `src/core/uds_core.c` (the `uds_internal_handle_secured_data` definition and its forward decl in `uds_internal.h`); table line 90.

**Note:** This handler runs the inner dispatch via `handle_request` → `execute_handler`, then secures the captured inner response and emits the outer. Apply the recipe to its own emission calls. The inner dispatch path is unchanged (it already routes through `execute_handler`, which now prefers `handler_v2` for migrated inner services). Verify the secure-capture suppression still zeroes `secure_capture_len`.

- [ ] **Step 1:** Apply the Migration Recipe to `uds_internal_handle_secured_data`.
- [ ] **Step 2:** Flip table entry (90) to `handler_v2`.
- [ ] **Step 3:** Build, run full suite. Expected: all PASS. Run `test_service_84` specifically (the secure path is the highest-risk migration).
- [ ] **Step 4:** clang-format (Docker) + commit: `refactor(0x84): migrate secured-data transmission`.

---

## Task 13: Remove legacy column, flip public typedef, v2.0.0

**Files:**
- Modify: `include/uds/uds_config.h` — delete the legacy `handler` field, rename `handler_v2` → `handler`; flip the public `uds_service_handler_t` typedef to the out-param form
- Modify: `src/core/uds_core.c` — `execute_handler` keeps only the v2 branch; table entries drop the `NULL,` legacy slot
- Modify: `src/core/uds_internal.h` — drop the temporary `uds_handler_v2_t` (now the canonical type)
- Modify: `examples/custom_service/main.c` — migrate the user handler to the out-param form
- Modify: any cmocka test that registers a `user_services` handler (search: `grep -rl "user_services" tests/`)
- Modify: `include/uds/uds_version.h` — bump to `2.0.0`
- Modify: `CHANGELOG.md` — breaking-change migration note

**Interfaces:**
- Produces: final public contract `typedef void (*uds_service_handler_t)(struct uds_ctx*, const uint8_t*, uint16_t, uds_result_t*);` and `uds_service_entry_t.handler` of that type.

- [ ] **Step 1: Confirm every core table entry uses `handler_v2`**

Run: `grep -n "uds_internal_handle" src/core/uds_core.c | grep -v handler_v2 | grep "{UDS_SID"`
Expected: no output (every entry migrated). If any remain, a prior task is incomplete — STOP.

- [ ] **Step 2: Flip the public typedef and collapse the struct**

In `include/uds/uds_config.h`: set `uds_service_handler_t` to `void (*)(struct uds_ctx*, const uint8_t*, uint16_t, uds_result_t*)`; remove the legacy `handler` field; rename `handler_v2` to `handler`.

- [ ] **Step 3: Collapse `execute_handler` to the v2 branch only**

Delete the `else { legacy }` block; the `if (service->handler_v2 != NULL)` guard becomes an unconditional call to `service->handler(...)`. Keep the emission switch and the post-emit reset block.

- [ ] **Step 4: Fix table initializers**

Remove the now-duplicated `NULL,` legacy slot from each entry so the single function pointer sits in the `handler` column. Run `cmake --build build` and fix any positional-initializer mismatches the compiler flags.

- [ ] **Step 5: Migrate `examples/custom_service` and `user_services` tests**

Apply the Migration Recipe to the example's handler and to each test handler found by `grep -rl "user_services" tests/`.

- [ ] **Step 6: Bump version and CHANGELOG**

`uds_version.h` → `2.0.0`. Add to `CHANGELOG.md`:
```
## 2.0.0
### Breaking
- Service handlers (`config.user_services`) now use the result-descriptor
  contract: `void handler(uds_ctx_t*, const uint8_t*, uint16_t, uds_result_t*)`.
  Replace `return uds_send_response(ctx, n);` with `uds_ok(out, n);`,
  `return uds_send_nrc(ctx, sid, nrc);` with `uds_nrc(out, nrc);`, and
  `return UDS_PENDING;` with `uds_pending(out);`. The framework now owns
  emission, suppressPosRsp, and ECUReset response/ordering.
```

- [ ] **Step 7: Build, run full suite + examples**

Run: `cmake --build build && ctest --test-dir build` and build the examples.
Expected: all PASS, byte-identical. Canaries green.

- [ ] **Step 8: clang-format (Docker) over src/include/examples + commit**

```bash
./scripts/docker_run.sh bash -c "find src include examples -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"
git add -A && git commit -m "refactor!: framework-owned response emission; v2.0.0 handler contract"
```

---

## Done criteria

- 27 handlers migrated; `uds_send_response`/`uds_send_nrc` no longer called from any service handler (`grep -rn "uds_send_response\|uds_send_nrc" src/services/` → only the public shims' own definitions remain in core).
- Single suppress site in `execute_handler`; single post-emit reset site.
- Full suite + examples green at every commit; clang-format-18 clean.
- `#76`/`#80` canaries green throughout.
- Version 2.0.0 with CHANGELOG migration note.
- PR opened against `develop`, `Closes` nothing (no issue) but references the spec; core-gate green before merge.
