# TX-complete before ECU reset (issue #88) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hold the ECU reset until the positive response is physically transmitted, via an optional `fn_tx_complete` library hook, and make the H5/H563 example do a real silicon reset.

**Architecture:** Add an optional `fn_tx_complete(ctx)` callback + `reset_tx_wait_ms` budget to `uds_config_t`. `uds_internal_run_pending_reset` polls the hook (bounded by the budget, never hangs) before calling `fn_reset`. NULL hook = today's behaviour. The example firmware implements the hook by polling `FDCAN.TXBRP` and resets via `SCB->AIRCR` SYSRESETREQ.

**Tech Stack:** C (C99), cmocka host unit tests, gcov, arm-none-eabi for the example, clang-format 18 (CI Docker).

## Global Constraints

- License header on every source file: `Copyright (c) 2026 Andrii Shylenko` / `SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0` (copy verbatim from a sibling file).
- PRs target `develop`. Before starting and before opening the PR: `git fetch origin develop && git merge origin/develop` (merge, never rebase). Keep the branch non-drifting.
- Commit author: `w1ne <14119286+w1ne@users.noreply.github.com>`. No Claude/AI references in commit messages.
- MISRA scan: no bare hex literals in `src/core/uds_core.c` comments.
- clang-format 18 (validate inside the CI Ubuntu-24.04 Docker image; local v22/v14 differ).
- Only `UDS_LOG_ERROR` (0) and `UDS_LOG_INFO` (1) exist — use `UDS_LOG_INFO`.
- `bool` is available in `uds_config.h` (`<stdbool.h>` at line 18).

---

### Task 1: Library — `fn_tx_complete` hook + bounded gate

**Files:**
- Modify: `include/uds/uds_config.h` (add typedef ~line 104, struct fields near `fn_reset` line 249, a default-budget macro near the log macros)
- Modify: `src/core/uds_core.c:712-721` (`uds_internal_run_pending_reset`)
- Test: `tests/unit/test_service_11.c`

**Interfaces:**
- Produces: `typedef bool (*uds_tx_complete_fn)(struct uds_ctx *ctx);`, config fields `uds_tx_complete_fn fn_tx_complete;` and `uint16_t reset_tx_wait_ms;`, macro `UDS_DEFAULT_RESET_TX_WAIT_MS`.
- Consumes: existing `uds_get_time_fn get_time_ms` (returns `uint32_t`), `uds_internal_log`, `ctx->reset_pending`, `ctx->reset_pending_type`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/test_service_11.c` (after the existing ordering probes):

```c
/* --- issue #88: TX-complete gate before reset --- */
static uint32_t g_fake_now;
static uint32_t g_fake_now_step;
static uint32_t fake_now(void)
{
    uint32_t t = g_fake_now;
    g_fake_now += g_fake_now_step;
    return t;
}

static int g_txc_calls;
static int g_txc_true_after;     /* return true once call count exceeds this */
static int g_reset_at_txc_calls; /* g_txc_calls observed when reset fired */

static bool fake_tx_complete(uds_ctx_t *ctx)
{
    (void) ctx;
    g_txc_calls++;
    return g_txc_calls > g_txc_true_after;
}

static void gate_reset_cb(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    g_reset_called++;
    g_reset_at_txc_calls = g_txc_calls;
}

/* Reset must wait until fn_tx_complete reports the frame is on the wire. */
static void test_ecu_reset_waits_for_tx_complete(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send      = order_tp_send;
    cfg.fn_reset        = gate_reset_cb;
    cfg.fn_tx_complete  = fake_tx_complete;
    cfg.get_time_ms     = fake_now; /* override mock_get_time: no will_return needed */
    g_fake_now = 1000; g_fake_now_step = 0; /* time frozen => never times out */
    g_txc_calls = 0; g_txc_true_after = 3;
    g_reset_called = 0; g_reset_at_txc_calls = 0;

    uint8_t request[] = {0x11, 0x01};
    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_reset_called, 1);     /* reset happened */
    assert_true(g_txc_calls >= 4);           /* polled until true */
    assert_true(g_reset_at_txc_calls > 3);   /* only after tx_complete returned true */
    assert_int_equal(g_tx_buf[0], 0x51);
    assert_int_equal(g_tx_buf[1], 0x01);
}

/* A transport that never completes must not hang the ECU: reset is forced
 * after reset_tx_wait_ms. */
static void test_ecu_reset_tx_complete_timeout_forces_reset(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx(&ctx, &cfg);
    cfg.fn_tp_send      = order_tp_send;
    cfg.fn_reset        = gate_reset_cb;
    cfg.fn_tx_complete  = fake_tx_complete;
    cfg.get_time_ms     = fake_now;
    cfg.reset_tx_wait_ms = 50u;
    g_fake_now = 0; g_fake_now_step = 10; /* +10ms per call => crosses 50ms */
    g_txc_calls = 0; g_txc_true_after = 1000000; /* never true */
    g_reset_called = 0;

    uint8_t request[] = {0x11, 0x01};
    uds_input_sdu(&ctx, request, sizeof(request));

    assert_int_equal(g_reset_called, 1);  /* forced despite tx never complete */
    assert_true(g_txc_calls < 100);       /* loop terminated, no hang */
}
```

Register both in the `CMUnitTest` array at the bottom of the file:

```c
    cmocka_unit_test(test_ecu_reset_waits_for_tx_complete),
    cmocka_unit_test(test_ecu_reset_tx_complete_timeout_forces_reset),
```

- [ ] **Step 2: Run tests, verify they fail to compile (field absent)**

Run: `cmake --build build --target test_service_11 && ./build/tests/unit/test_service_11` (or the repo's standard `ctest` invocation)
Expected: compile error — `uds_config_t has no member named 'fn_tx_complete'`.

- [ ] **Step 3: Add the typedef, fields, and macro to `uds_config.h`**

After `typedef void (*uds_reset_fn)(struct uds_ctx *ctx, uint8_t type);` (line 104):

```c
/* Optional. Returns true once the most recently transmitted response is
 * physically on the wire (transport TX buffer/mailbox drained). When set,
 * uds_internal_run_pending_reset waits for it (bounded by reset_tx_wait_ms)
 * before invoking fn_reset, so a rebooting fn_reset cannot drop the just-sent
 * ECUReset positive response. NULL keeps the legacy immediate-reset behaviour.
 * get_time_ms must advance while this returns false. */
typedef bool (*uds_tx_complete_fn)(struct uds_ctx *ctx);
```

Near the log-level macros (after `#define UDS_LOG_INFO 1`):

```c
/* Default budget (ms) to wait for fn_tx_complete before forcing the reset. */
#define UDS_DEFAULT_RESET_TX_WAIT_MS 50u
```

In `uds_config_t`, immediately after `uds_reset_fn fn_reset;` (line 249) and its
`power_down_time` neighbour:

```c
    /** Optional: returns true when the last response has left the transport.
     *  Gates fn_reset so a rebooting reset cannot drop the response. */
    uds_tx_complete_fn fn_tx_complete;
    /** Max ms to wait for fn_tx_complete before forcing the reset (0 =
     *  UDS_DEFAULT_RESET_TX_WAIT_MS). Ignored when fn_tx_complete is NULL. */
    uint16_t reset_tx_wait_ms;
```

- [ ] **Step 4: Implement the gate in `uds_core.c`**

Replace `uds_internal_run_pending_reset` (lines 712-721):

```c
void uds_internal_run_pending_reset(uds_ctx_t *ctx)
{
    if (!ctx->reset_pending) {
        return;
    }
    ctx->reset_pending = false;
    if (ctx->config->fn_reset == NULL) {
        return;
    }

    if (ctx->config->fn_tx_complete != NULL) {
        uint16_t budget = (ctx->config->reset_tx_wait_ms != 0u)
                              ? ctx->config->reset_tx_wait_ms
                              : UDS_DEFAULT_RESET_TX_WAIT_MS;
        uint32_t start = ctx->config->get_time_ms();
        while (!ctx->config->fn_tx_complete(ctx)) {
            if ((uint32_t) (ctx->config->get_time_ms() - start) >= (uint32_t) budget) {
                uds_internal_log(ctx, UDS_LOG_INFO,
                                 "reset: TX-complete wait timed out, forcing reset");
                break;
            }
        }
    }

    ctx->config->fn_reset(ctx, ctx->reset_pending_type);
}
```

- [ ] **Step 5: Run the full 0x11 suite, verify pass**

Run: `cmake --build build --target test_service_11 && ./build/tests/unit/test_service_11`
Expected: PASS, including the two new tests and all existing (backward-compat: tests without `fn_tx_complete` set NULL → immediate reset, unchanged).

- [ ] **Step 6: clang-format + commit**

```bash
clang-format -i include/uds/uds_config.h src/core/uds_core.c tests/unit/test_service_11.c
git add include/uds/uds_config.h src/core/uds_core.c tests/unit/test_service_11.c
git -c user.name='w1ne' -c user.email='14119286+w1ne@users.noreply.github.com' \
  commit -m "feat(0x11): gate ECU reset on optional fn_tx_complete (issue #88)"
```

---

### Task 2: Example firmware — real TXBRP-poll + NVIC_SystemReset

**Files:**
- Modify: `examples/h5_uds_ecu_full/firmware/main.c` (register block near line 99, `fn_reset` at line 260, config wiring near line 533)

**Interfaces:**
- Consumes: Task 1's `cfg.fn_tx_complete`.
- Produces: nothing other tasks depend on.

- [ ] **Step 1: Add the TXBRP register define**

Next to `#define FDCAN_REG_TXBAR 0x0CCu` (line ~99):

```c
#define FDCAN_REG_TXBRP 0x0C8u /* TX Buffer Request Pending */
```

- [ ] **Step 2: Implement `fn_tx_complete` and a real `fn_reset`**

Replace the stub `fn_reset` (lines 260-265) with:

```c
/* True once every FDCAN TX buffer has been transmitted (mailbox drained):
 * TXBRP == 0. Gates the reset so the 0x51 response reaches the bus first. */
static bool fn_tx_complete(uds_ctx_t *ctx)
{
    (void) ctx;
    return REG32(fdcan_reg(FDCAN_REG_TXBRP)) == 0u;
}

/* Real Cortex-M system reset via SCB->AIRCR SYSRESETREQ. Never returns. */
static void fn_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    (void) type;
    uart_puts("ECU_RESET\n");
    volatile uint32_t *aircr = (volatile uint32_t *) 0xE000ED0Cu;
    __asm volatile("dsb 0xF" ::: "memory");
    *aircr = (uint32_t) ((0x5FAu << 16) | (*aircr & 0x700u) | (1u << 2));
    __asm volatile("dsb 0xF" ::: "memory");
    for (;;) {
    }
}
```

- [ ] **Step 3: Wire the hook into the config**

After `cfg.fn_reset = fn_reset;` (line ~533):

```c
    cfg.fn_tx_complete = fn_tx_complete; /* hold reset until 0x51 is on the wire */
```

- [ ] **Step 4: Build the example, verify it compiles**

Run: `make -C examples/h5_uds_ecu_full/firmware` (or the example's documented build)
Expected: builds clean; `fn_reset` no longer references only `uart_puts`. Behavioural validation (response-on-wire-then-reboot) happens on the labwired virtual H563 (labwired-core #336 async-TX) and on the wired board — not in host unit tests, by design.

- [ ] **Step 5: clang-format + commit**

```bash
clang-format -i examples/h5_uds_ecu_full/firmware/main.c
git add examples/h5_uds_ecu_full/firmware/main.c
git -c user.name='w1ne' -c user.email='14119286+w1ne@users.noreply.github.com' \
  commit -m "feat(example): real H5 reset waits for FDCAN TX-complete (issue #88)"
```

---

### Task 3: CHANGELOG + sync + PR

**Files:**
- Modify: `CHANGELOG.md` (top, new unreleased/next-version entry)

- [ ] **Step 1: Add the CHANGELOG entry**

Under the next version heading:

```markdown
### Added
- ECU Reset (0x11): optional `fn_tx_complete` config hook and `reset_tx_wait_ms`
  budget so a rebooting `fn_reset` waits until the positive response is on the
  wire before resetting (issue #88). H5 example now performs a real
  `NVIC_SystemReset` after polling FDCAN TXBRP.
```

- [ ] **Step 2: Sync with develop (no drift)**

```bash
git fetch origin develop
git merge --no-edit origin/develop
```
Resolve any conflicts (merge, never rebase). Re-run `./build/tests/unit/test_service_11`.

- [ ] **Step 3: Commit + push + open PR → develop**

```bash
git add CHANGELOG.md
git -c user.name='w1ne' -c user.email='14119286+w1ne@users.noreply.github.com' \
  commit -m "docs(changelog): TX-complete reset gate (issue #88)"
git push -u origin fix/issue88-tx-complete
gh pr create --repo w1ne/udslib --base develop \
  --title "fix(0x11): wait for TX-complete before ECU reset (issue #88)" \
  --body "Closes #88. Adds optional fn_tx_complete hook + reset_tx_wait_ms so a rebooting fn_reset cannot drop the 0x51 response (real-HW race). Backward compatible (NULL hook = legacy). H5 example does a real NVIC_SystemReset after polling FDCAN TXBRP. Host tests prove the gate logic; on-wire-before-reboot is proven on the labwired virtual H563 (labwired-core #336) and the wired board."
```

## Self-Review

- Spec coverage: `fn_tx_complete` hook + budget (Task 1) ✓; suppressPosRsp path (no queued frame → TXBRP==0 → immediate, no special-case) ✓; 0x84-deferred path (same centralised `run_pending_reset`) ✓; firmware real reset + TXBRP poll (Task 2) ✓; honest test split (Task 1 host gate, sim/HW elsewhere) ✓; CHANGELOG + no-drift sync (Task 3) ✓.
- Placeholder scan: none.
- Type consistency: `fn_tx_complete` / `uds_tx_complete_fn` / `reset_tx_wait_ms` / `UDS_DEFAULT_RESET_TX_WAIT_MS` used identically across config, core, and tests.
