# Design: ResponseOnEvent (0x86) — autonomous event engine

**Date:** 2026-06-19
**Status:** Proposed — awaiting sign-off before implementation
**Origin:** Last service for full ISO 14229-1 coverage (26/27 → 27/27). Unlike 0x24/0x2C/0x38/0x84, 0x86 is *stateful*: the server stores event definitions and later emits responses on its own from `uds_process()`.

## Problem
ResponseOnEvent lets a tester register "when event E happens, run service S and send me its response." The server must: store definitions, activate/deactivate them, detect events, and autonomously emit the registered service's response when an event fires — within an event window.

## Two mechanisms we already have to build on
1. **Periodic scheduler** in `uds_process()` (0x2A) — the model for time-driven servicing and wrap-safe deadline checks.
2. **Secured-capture dispatch** from 0x84 — `handle_request()` + `secure_capturing` lets us run an inner service and *capture* its response instead of sending it. ROE reuses this to execute the `serviceToRespondTo` and wrap the result.

## Scope (phase 1)
Implement these sub-functions:

| Sub | Name | Phase 1 |
|-----|------|---------|
| 0x00 | stopResponseOnEvent | ✅ deactivate all (keep stored) |
| 0x01 | onDTCStatusChange | ✅ |
| 0x03 | onChangeOfDataIdentifier | ✅ |
| 0x04 | reportActivatedEvents | ✅ |
| 0x05 | startResponseOnEvent | ✅ activate stored defs |
| 0x06 | clearResponseOnEvent | ✅ clear all |
| 0x02 | onTimerInterrupt | ⛔ NRC 0x12 (deferred) |
| 0x07 | onComparisonOfValues | ⛔ NRC 0x12 (deferred) |

Deferred sub-functions return `subFunctionNotSupported` (0x12) — explicit, not silent.

## Event detection model (chosen): application-notify
ISO leaves event *detection* to the implementation. The library cannot itself know a DID value or DTC status changed. So the application notifies the library when a real-world change occurs, via a new public API:

```c
/* Call when a monitored event occurs. The library checks active ROE
 * definitions; for each match it runs the stored serviceToRespondTo and
 * emits the ROE message. `param` is the DID (0x03) or the DTC (0x01). */
int uds_roe_trigger(uds_ctx_t *ctx, uint8_t event_type, uint32_t param);
```

Rejected alternative — *library-poll* (a callback the library calls each `uds_process()` asking "did event X fire?"): more overhead, awkward for value-change semantics, and forces the app to cache "previous" values anyway. App-notify is cheaper and matches how the app already knows about changes.

## State (new fields in `uds_ctx`)
A small fixed array of slots (default `UDS_ROE_MAX_EVENTS = 4`):

```c
typedef struct {
    bool     in_use;       /* slot occupied (setup done) */
    bool     active;       /* started (0x05), cleared by stop (0x00) */
    uint8_t  event_type;   /* 0x01 / 0x03 */
    uint32_t event_param;  /* DID (0x03) or DTC (0x01) */
    uint32_t window_deadline;  /* absolute ms; 0 = infinite */
    uint8_t  str[UDS_ROE_STR_MAX];  /* serviceToRespondToRecord bytes */
    uint8_t  str_len;
} uds_roe_slot_t;
```
`UDS_ROE_STR_MAX` default 8 (enough for e.g. `22 F1 90`). Stored on `ctx`, sized by compile-time macros so the cost is opt-out-able.

## Request / response shapes (ISO 14229-1 §)
- **Setup** (0x01/0x03): `86 <eventType> <eventWindowTime> <eventTypeRecord...> <serviceToRespondToRecord...>`. The library stores the slot and replies `C6 <eventType> <numberOfActivatedEvents> <eventWindowTime> <eventTypeRecord> <serviceToRespondToRecord>` (echo).
- **start/stop/clear** (0x05/0x00/0x06): `86 <sub>` → `C6 <sub> <numberOfActivatedEvents> <eventWindowTime=0>`.
- **reportActivatedEvents** (0x04): `C6 04 <count>` followed by one record per active slot.
- **Event emission** (from `uds_roe_trigger`): `C6 <eventType> <numberOfIdentifiedEvents=1> <eventWindowTime>` followed by the captured `serviceToRespondTo` response bytes.

(Exact record byte counts per ISO are finalized in implementation; tests assert them.)

## Event window
`window_deadline` checked in `uds_process()` (same wrap-safe pattern as periodic/S3). On expiry the slot is deactivated (`active = false`). `eventWindowTime = 0x02` (infinite) stores deadline 0 → never expires.

## suppressPosRsp / storageState bits
The eventType byte carries bit 0x80 (suppressPosRsp on the *setup* ack) and bit 0x40 (storageState). Phase 1 honors suppressPosRsp on setup; storageState (persist across power cycles) is parsed but treated as volatile (documented limitation).

## Error handling
- Unsupported sub-function (0x02/0x07/other) → NRC 0x12.
- Setup with no room (all slots in use) → NRC 0x22 (ConditionsNotCorrect).
- Malformed length → NRC 0x13.
- `serviceToRespondTo` that is itself 0x86 or 0x84 → NRC 0x31 (no recursion).
- `uds_roe_trigger` for an event with no active matching slot → no-op, returns 0.

## Tests (cmocka, host_sim) — `test_service_86.c`
- setup (0x03) stores a slot and acks; start activates; `uds_roe_trigger(0x03, did)` emits the captured RDBI response.
- onDTCStatusChange (0x01) round-trip.
- stop deactivates (trigger after stop emits nothing); clear empties; reportActivatedEvents lists active slots.
- window expiry in `uds_process()` deactivates the slot.
- deferred sub-function (0x02) → NRC 0x12; no-room → 0x22; recursion (serviceToRespondTo = 0x86) → 0x31.

## Out of scope (phase 1)
- onTimerInterrupt (0x02), onComparisonOfValues (0x07).
- Non-volatile persistence of stored events (storageState treated volatile).
- Multiple simultaneous identified events per trigger (emit one per matching slot, sequentially).

## Cross-cutting
- Additive: all new state/API guarded so existing builds are unchanged; gated by `UDS_ROE_MAX_EVENTS` (set to 0 to compile ROE out entirely — falls back to `serviceNotSupported`).
- MISRA, clang-format-14, cppcheck gates. Branch off current work; targets `develop`. Bumps to 1.18.0.
