# udslib #88 — TX-complete before ECU reset (real-HW fix)

- Status: draft, pending user review
- Date: 2026-06-23
- Repo: udslib (branch `fix/issue88-tx-complete`, PR → `develop`)
- Issue: [#88](https://github.com/w1ne/udslib/issues/88)
- Companion: labwired-core [#336](https://github.com/w1ne/labwired-core/issues/336) (FDCAN async-TX so the sim can't false-pass this), and the active `feat/scriptable-uds-tester` branch (real SCB reset + scriptable tester) which reproduces #88 in-sim.

## Problem

`uds_internal_run_pending_reset` (`src/core/uds_core.c:712`) invokes `fn_reset` the instant `uds_send_response` → `fn_tp_send` returns. `fn_tp_send` returning means the frame is **queued to the FDCAN TX mailbox**, not **arbitrated onto the bus**. A firmware whose `fn_reset` calls `NVIC_SystemReset()` reboots before the mailbox transmits → `SYSRESETREQ` resets FDCAN → the in-flight `51 01` is lost → the tester sees silence after `11 01`, and only `11 81` (suppressPosRsp) appears to work. This is exactly issue #88.

v1.20.0 fixed only the *source ordering* (build/send the response before calling `fn_reset`). The hardware TX-complete race is untouched, and the H5/H563 example `fn_reset` is a `uart_puts("ECU_RESET\n")` stub (`examples/h5_uds_ecu_full/firmware/main.c:260`) — so the real path was never exercised. The reporter, who *does* call `NVIC_SystemReset`, hits the race.

## Goals

1. Give the library a real mechanism to hold the reset until the response frame is physically transmitted — so the integrator cannot get this wrong (the reporter did).
2. Make the H5/H563 example perform a real, silicon-correct reset that waits for TX-complete first.
3. Keep it backward compatible and MISRA-clean; no behavior change for configs that don't opt in.

Non-goals: changing ISO-TP, the 0x84 deferral latch, or the v1.20.0 ordering. This adds a gate in front of the existing `fn_reset` invocation.

## Design

### Library: optional `fn_tx_complete` hook

Add to `uds_config_t`:

```c
/* Optional. Returns true once the most recently transmitted response is
 * physically on the wire (transport TX buffer/mailbox drained). When set,
 * the library waits for it to return true — bounded by reset_tx_wait_ms —
 * before invoking fn_reset, so a rebooting fn_reset cannot drop the
 * just-sent positive response (ISO 14229-1 ECUReset). NULL = legacy
 * behaviour (fn_reset fires immediately). */
typedef bool (*uds_tx_complete_fn)(uds_ctx_t *ctx);
uds_tx_complete_fn fn_tx_complete;

/* Max time (ms, via get_time_ms) to wait for fn_tx_complete before
 * forcing the reset anyway, so a stuck transport can never hang the ECU.
 * 0 = use UDS_DEFAULT_RESET_TX_WAIT_MS. Ignored when fn_tx_complete NULL. */
uint16_t reset_tx_wait_ms;
```

`uds_internal_run_pending_reset` becomes:

```c
void uds_internal_run_pending_reset(uds_ctx_t *ctx)
{
    if (!ctx->reset_pending) return;
    ctx->reset_pending = false;
    if (ctx->config->fn_reset == NULL) return;

    if (ctx->config->fn_tx_complete != NULL) {
        uint16_t budget = ctx->config->reset_tx_wait_ms
                          ? ctx->config->reset_tx_wait_ms
                          : UDS_DEFAULT_RESET_TX_WAIT_MS;
        uint32_t start = ctx->config->get_time_ms();
        while (!ctx->config->fn_tx_complete(ctx)) {
            if ((uint32_t)(ctx->config->get_time_ms() - start) >= budget) {
                uds_internal_log(ctx, UDS_LOG_WARN,
                                 "reset: TX-complete wait timed out");
                break;
            }
        }
    }
    ctx->config->fn_reset(ctx, ctx->reset_pending_type);
}
```

Properties:

- **Centralised** — every reset path benefits: the normal/suppressed 0x11 path (`uds_service_maintenance.c:77`) and the 0x84-deferred path (`uds_core.c:401,425`) both funnel through `run_pending_reset`.
- **suppressPosRsp (`11 81`)** needs no special case: nothing was queued, so `fn_tx_complete` (TXBRP empty) returns true on the first poll → immediate reset, as today.
- **Backward compatible** — `fn_tx_complete == NULL` is the existing behaviour byte-for-byte.
- **No hang** — the `reset_tx_wait_ms` budget forces the reset even if the transport never reports complete (logged).

### Firmware: real reset that waits for TX (`examples/h5_uds_ecu_full/firmware/main.c`)

- Add `#define FDCAN_REG_TXBRP 0x0C8u` (TX Buffer Request Pending).
- `fn_tx_complete` → `return (REG32(fdcan_reg(FDCAN_REG_TXBRP)) == 0u);` (all TX buffers drained).
- `fn_reset` → real Cortex-M reset:
  ```c
  #define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0Cu)
  __asm volatile ("dsb 0xF" ::: "memory");
  SCB_AIRCR = (0x5FAu << 16) | (SCB_AIRCR & 0x700u) | (1u << 2); /* SYSRESETREQ */
  __asm volatile ("dsb 0xF" ::: "memory");
  for (;;) { }
  ```
- Wire `cfg.fn_tx_complete = fn_tx_complete;` (leave `reset_tx_wait_ms = 0` → default). `main()` already prints `ECU_READY` on boot (`:561`), which is the reboot-detection banner the labwired #88 smoke asserts.

The same code is correct on real STM32H563 silicon (TXBRP is the M_CAN register the reporter's HAL also uses) and, once labwired-core #336 lands async-TX, on the virtual device — with no shim on either.

## Testing

Honest separation of what proves what:

- **cmocka host unit tests** (`tests/unit/test_service_11.c`) — prove the *library gate logic*:
  1. `fn_tx_complete` returns false 3×, then true: assert `fn_reset` is not called until after the true, and is called exactly once (ordering via a shared monotonic counter, same style as the existing `g_send_order`/`g_reset_order`).
  2. Timeout: `fn_tx_complete` always false, fake `get_time_ms` advanced past `reset_tx_wait_ms`: assert `fn_reset` still fires (no hang) and a warning is logged.
  3. Backward compat: no `fn_tx_complete` → `fn_reset` called immediately (existing tests stay green).
- **labwired virtual H563** (companion, issue #336 + scriptable-tester branch) — proves the real *on-wire-before-reboot* behaviour with async TX + real SCB reset. The host test cannot model a non-returning reset; that faithful half lives in the sim, not behind a host shim.
- **Real HW** — flash the example to the wired H563, drive `11 01` then a follow-up service, confirm `51 01` then a normal reply post-reboot.

Gate: full cmocka suite + gcov (every 0x11 path covered), clang-format 18 in the CI Docker image.

## Risks

- `get_time_ms` must advance during the wait (true on bare-metal systick; fakeable in host tests). Documented in the hook's doc-comment.
- A transport that never drains would, without the budget, hang the ECU — mitigated by `reset_tx_wait_ms`.
- New config fields shift `uds_config_t` layout — append at the existing reset-related grouping; bump CHANGELOG; no ABI promise on the struct (header-built).
