# DTC Enrichment + ReadDTCInformation (0x19) Expansion

**Issue:** w1ne/udslib#39
**Date:** 2026-06-19
**Status:** Design — approved in brainstorming, pending spec review

## Problem

The DTC model in udslib carries only `{ dtc, status }`. The library supports a
subset of ReadDTCInformation (0x19) subfunctions — 0x01, 0x02, 0x04, 0x06, 0x0A.
Issue #39 asks for richer DTC attributes (severity, fault-detection counter,
aging counter, functional unit, functional group identifier), DTC classification
(P/C/B/U), and the ability to "manage instances," integrated into 0x19.

## Design principle (the lean boundary)

udslib's rule, stated in the code, is: **the library owns the ISO 14229-1 wire
layout; the application owns state and policy.** That separation keeps the core
small and allocation-free. This design preserves it:

- **Core (always compiled):** additive struct fields + wire-formatting for the
  new subfunctions. Reuses the *existing* `fn_dtc_list` hook — **no new
  enumeration callbacks**. One new config field. Zero library-owned DTC storage.
- **Optional helper (`uds_dtc_store`, opt-in, separate file):** a thin reference
  implementation of the callbacks over an **application-provided** array. This is
  where diagnostic *policy* (counter increment/decrement, aging, self-heal)
  lives — deliberately out of the protocol core. The core never depends on it.

Diagnostic policy that does not generalize (when an operation cycle starts, when
to increment the fault-detection counter, the aging threshold) stays in the
opt-in helper, never in the core.

## Scope (locked in brainstorming)

- New subfunctions: **0x07, 0x08, 0x09, 0x14, 0x42, 0x55** (Classic + WWH-OBD).
- Stateless DTC classification decoder + named status/severity/FGID macros.
- Optional reference DTC store helper, shipped in this work.
- Aging counter and fault-detection counter exposed; aging served as extended
  data (no new top-level subfunction — aging is not a standard 0x19 subfunction).

Explicitly **out of scope** (lean): no freeze-frame/snapshot capture engine (the
store leaves snapshots to the existing app-provided `fn_dtc_snapshot`), no NVM
persistence in the store, no threading/locking, no dynamic allocation.

## Components

### 1. Extended `uds_dtc_record_t` (additive, source-compatible)

```c
typedef struct {
    uint32_t dtc;                     /* 3-byte DTC, right-aligned        */
    uint8_t  status;                  /* statusOfDTC (Annex D)            */
    uint8_t  severity;                /* DTCSeverity  (0x08/0x09/0x42)    */
    uint8_t  functional_unit;         /* DTCFunctionalUnit (0x08/0x09)    */
    int8_t   fault_detection_counter; /* signed -128..127  (0x14)         */
    uint8_t  aging_counter;           /* op-cycles since last fault       */
    uint8_t  functional_group;        /* WWH-OBD group (0x33/0xD0/0xFE)   */
} uds_dtc_record_t;
```

Existing initializers (`{0x123456u, 0x09u}`) still compile; new fields zero-init.
The 0x01/0x02/0x0A wire path continues to read only `dtc` + `status`, so this is
a non-breaking, additive change for a source library.

### 2. Stateless helpers (header-only / pure)

- `uds_dtc_category_t uds_dtc_category(uint32_t dtc)` → category from the top two
  bits of the high DTC byte: `(dtc >> 22) & 0x3u`
  (`UDS_DTC_POWERTRAIN=0`, `UDS_DTC_CHASSIS=1`, `UDS_DTC_BODY=2`, `UDS_DTC_NETWORK=3`).
- `UDS_DTC_STATUS_*` macros — bits 0..7: testFailed, testFailedThisOperationCycle,
  pendingDTC, confirmedDTC, testNotCompletedSinceLastClear, testFailedSinceLastClear,
  testNotCompletedThisOperationCycle, warningIndicatorRequested.
- `UDS_DTC_SEVERITY_*` macros — maintenanceOnly (0x20), checkAtNextHalt (0x40),
  checkImmediately (0x80), plus the GTR DTCClass bits (0x01..0x10).
- `UDS_DTC_FGID_*` macros — EMISSIONS (0x33), SAFETY (0xD0), VOBD (0xFE).

These are pure and trivially unit-tested.

### 3. New 0x19 subfunctions (library formats the wire layout)

All enumeration reuses the existing `fn_dtc_list(ctx, status_mask, out, max)` hook
(which now returns the richer record). The library applies any additional
severity / FDC / functional-group filtering inline — no new hook.

| Sub | Name | Per-record wire layout |
|-----|------|------------------------|
| 0x07 | reportNumberOfDTCBySeverityMaskRecord | count only (like 0x01, filtered by severity+status) |
| 0x08 | reportDTCBySeverityMaskRecord | `severity(1) functionalUnit(1) DTC(3) status(1)` |
| 0x09 | reportSeverityInformationOfDTC | single record, same layout as 0x08 |
| 0x14 | reportDTCFaultDetectionCounter | `DTC(3) FDC(1 signed)`, only in-progress DTCs (FDC 0x01–0x7E) |
| 0x42 | reportWWHOBDDTCByMaskRecord | filtered by functionalGroup; header carries DTCSeverityAvailabilityMask + DTCFormatId; records `severity(1) DTC(3) status(1)` |
| 0x55 | reportWWHOBDDTCWithPermanentStatus | thin variant of 0x42 over permanent-status DTCs |

Supporting core changes:
- Widen `UDS_MASK_SUB_19` (sub-function bitmask in `uds_internal.h`) to admit the
  new sub-IDs.
- Add request-length / status-mask validation for the new subfunctions to the
  existing checks in `uds_internal_handle_read_dtc_info`.
- Exact byte layouts are pinned against ISO 14229-1 in the implementation plan.

### 4. New config fields (lean: two pointers/bytes total)

```c
uint8_t dtc_severity_availability_mask; /* reported in 0x08/0x42/0x55 responses */
void   *app_data;                       /* opaque app/store handle, recoverable
                                           inside callbacks via ctx->config->app_data */
```

`app_data` is the store-wiring mechanism: `uds_ctx` carries no user pointer today,
so the store recovers itself inside callbacks via `ctx->config->app_data` rather
than a global. It is generally useful to any application and costs one pointer.

### 5. Optional reference DTC store — `uds_dtc_store.{h,c}`

Opt-in, separate translation unit. Application owns the backing array (no
allocation). The store *implements the callbacks*; the core never references it.

```c
typedef struct {
    uds_dtc_record_t *entries;
    uint16_t          capacity;
    uint16_t          count;
    uint8_t           aging_threshold;   /* op-cycles to self-heal (e.g. 40) */
} uds_dtc_store_t;

void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing,
                        uint16_t capacity, uint8_t aging_threshold);
int  uds_dtc_store_register(uds_dtc_store_t *s, uint32_t dtc, uint8_t severity,
                            uint8_t functional_unit, uint8_t functional_group);
uds_dtc_record_t *uds_dtc_store_get(uds_dtc_store_t *s, uint32_t dtc);
void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed);
void uds_dtc_store_operation_cycle(uds_dtc_store_t *s);
void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group);

/* Ready-made callbacks (assign to uds_config_t, with app_data = &store): */
int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask,
                          uds_dtc_record_t *out, uint16_t max);
int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len);
int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group);
```

Policy lives here, not in the core:
- `report_test(failed=true)` increments FDC (saturating at +0x7F → sets
  testFailed/confirmed status bits, resets aging); `failed=false` decrements FDC.
- `operation_cycle()` ages each not-currently-failed DTC; when `aging_counter`
  reaches `aging_threshold`, the DTC self-heals (entry cleared).
- `extdata_cb` emits aging counter and FDC as extended data records (the only
  place aging is exposed on the wire).
- Snapshots remain the application's responsibility via the existing
  `fn_dtc_snapshot` — the store does **not** capture freeze frames.

## Data flow

1. App registers DTCs into its store (or its own array) once at init and sets
   `cfg.app_data`, `cfg.fn_dtc_list`, `cfg.dtc_status_availability_mask`,
   `cfg.dtc_severity_availability_mask`.
2. App reports test results / operation cycles to the store as the ECU runs.
3. Tester sends 0x19 sub X → core validates → calls `fn_dtc_list` (+ inline
   severity/FDC/group filtering) → formats the sub-X wire layout → responds.

## Error handling

- Reuse existing NRCs: `UDS_NRC_INCORRECT_LENGTH` (request too short for the sub),
  `UDS_NRC_RESPONSE_TOO_LONG` (batch exceeds tx buffer), `UDS_NRC_CONDITIONS_NOT_CORRECT`
  (no `fn_dtc_list` wired). Subfunction-not-supported handled by the widened
  `UDS_MASK_SUB_19`.
- Store register beyond `capacity` returns a negative error; duplicate register
  updates in place.

## Testing (cmocka)

- Wire-layout test per new subfunction (0x07/0x08/0x09/0x14/0x42/0x55), asserting
  exact response bytes against ISO 14229-1.
- Classification decoder: P/C/B/U for the issue's examples (`0x012345`→P,
  `0xDCBA98`→U, `0xFFFFFF`→U) and boundary values.
- Store: register/get/clear, FDC increment/decrement saturation, aging tick and
  self-heal at threshold.
- Back-compat: existing `test_service_dtc.c` cases still pass unchanged.

## Example & docs

- An example wiring the issue's three DTCs (`0x012345`, `0xDCBA98`, `0xFFFFFF`)
  through the store and answering each new subfunction.
- CHANGELOG `[Unreleased]` entry (referencing #39); doxygen on all new public API;
  update `docs/SERVICE_COMPLIANCE.md` 0x19 coverage.

## Implementation phases

1. Extended struct + stateless helpers (classification, named macros) + tests.
2. New subfunctions + config fields + widened subfunction mask + tests.
3. Optional `uds_dtc_store` helper + tests.
4. Example + CHANGELOG + doxygen + compliance doc.

Targets `develop`. clang-format-14 gate; cmocka unit tests; C99, `-Wall -Wextra`.
