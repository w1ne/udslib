# DTC store example

Shows the **optional reference DTC store** (`uds/uds_dtc_store.h`) managing DTC
instances and serving `ReadDTCInformation` (0x19) end-to-end — without the
application writing any wire-format code.

The store is opt-in and uses an **application-provided array** (no `malloc`). It
implements the library callbacks for you, so wiring it up is three lines of
config. The protocol core does not depend on it.

## The whole integration

```c
/* 1. Give the store a fixed backing array and an aging threshold. */
static uds_dtc_record_t backing[8];
static uds_dtc_store_t store;
uds_dtc_store_init(&store, backing, 8u, 40u);

/* 2. Register DTCs (number, severity, functional unit, functional group). */
uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY,
                       0x10u, UDS_DTC_FGID_EMISSIONS);

/* 3. Publish the live environment, then report test results.
 *    The freeze frame is stored when the DTC confirms (the
 *    fault-detection counter reaches +127), not on the first sample. */
uds_dtc_snapshot_t env = { .voltage = 0x8Cu, .power_mode = 0x02u };
uds_dtc_store_set_environment(&store, &env);
uds_dtc_store_report_test(&store, 0x012345u, true);

/* 4. Point the config at the store and its ready-made callbacks. */
cfg.app_data       = &store;
cfg.fn_dtc_list    = uds_dtc_store_list_cb;     /* 0x01/0x02/0x0A, severity, ... */
cfg.fn_dtc_snapshot = uds_dtc_store_snapshot_cb; /* 0x04 freeze frame */
cfg.fn_dtc_extdata = uds_dtc_store_extdata_cb;   /* 0x06 occurrence/pending/aged/ageing */
cfg.fn_dtc_clear   = uds_dtc_store_clear_cb;     /* ClearDiagnosticInformation (0x14) */
cfg.dtc_status_availability_mask = 0x7Fu;
```

`uds_dtc_category()` (from `uds/uds_dtc.h`) decodes a DTC's Powertrain / Chassis /
Body / Network class from its top two bits — no store needed.

The UDS stack never calls `uds_dtc_store_report_test()` or
`uds_dtc_store_operation_cycle()`. The ECU monitor calls `report_test` when a
test finishes. The ignition-cycle task calls `operation_cycle` when that cycle
ends. `0x19` only reads the table.

## Build & run

```sh
make run
```

Expected output:

```
=== DTC category classification ===
  DTC 0x012345 -> P (Powertrain)
  DTC 0xDCBA98 -> U (Network)
  DTC 0xFFFFFF -> U (Network)

=== ReadDTCInformation (0x19 0x02 0xFF) response ===
  Response (15 bytes): 59 02 7F 01 23 45 23 DC BA 98 23 FF FF FF 23
  DTC[0] = 0x012345  status=0x23  category=P (Powertrain)
  DTC[1] = 0xDCBA98  status=0x23  category=U (Network)
  DTC[2] = 0xFFFFFF  status=0x23  category=U (Network)

The stack does not call report_test. The monitor below confirms DTC 012345.
Call uds_dtc_store_operation_cycle() when the ignition cycle ends.

=== Snapshot (0x19 0x04) for DTC 012345 ===
  Response (20 bytes): 59 04 01 23 45 2F 01 02 10 01 19 06 13 0D 05 2A 10 02 8C 02
  DID 0x1001 time 2025-06-19 13:05:42, DID 0x1002 voltage 14.0 V, power mode 2.

=== Extended data (0x19 0x06) for DTC 012345 ===
  Response (11 bytes): 59 06 01 23 45 2F 01 01 01 00 00
  Counters: occurrence 1, pending 1, aged 0, ageing 0.
```

The `0x02` response is `59 02 <statusAvailabilityMask>` followed by one
`[DTC(3) statusOfDTC(1)]` record per matching DTC. Status `0x23` =
`testFailed | testFailedThisOperationCycle | testFailedSinceLastClear`, set by
one `uds_dtc_store_report_test(..., true)`.

The demo then confirms DTC `0x012345` (the store confirms when the
fault-detection counter reaches +127) after publishing voltage `0x8C`, power
mode `0x02`, and the time 2025-06-19 13:05:42. `0x04` returns that freeze
frame. `0x06` returns occurrence `1`, pending `1`, aged `0`, ageing `0`.
Status `0x2F` adds `confirmed` and `pending` to `0x23`.

## Persistence

The store is RAM. It does not write flash. `uds_dtc_store_serialize()` copies
status, the fault-detection counter, the aging counter, the extended-data
counters, and the freeze frame into a buffer the application stores in its own
NVM. `uds_dtc_store_deserialize()` writes those bytes back after the DTC
numbers are registered again at boot. Wire 0x04 and 0x06 with
`uds_dtc_store_snapshot_cb` and `uds_dtc_store_extdata_cb` after publishing the
live environment through `uds_dtc_store_set_environment()`.

`../dtc_persist` is a host demo of that, with a byte array standing in for flash.

## See also

`../dtc_full_coverage` — every 0x19 sub-function, including the ones the
application formats itself and the 0x04/0x06 freeze-frame payloads.
