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

/* 3. Report test results as the ECU runs; the store tracks the
 *    fault-detection counter, confirmation, and aging/self-heal. */
uds_dtc_store_report_test(&store, 0x012345u, true);

/* 4. Point the config at the store and its ready-made callbacks. */
cfg.app_data    = &store;
cfg.fn_dtc_list = uds_dtc_store_list_cb;   /* 0x01/0x02/0x0A and the
                                              severity / first / permanent
                                              sub-functions the library frames */
cfg.fn_dtc_clear = uds_dtc_store_clear_cb; /* ClearDiagnosticInformation (0x14) */
cfg.dtc_status_availability_mask = 0x7Fu;
```

`uds_dtc_category()` (from `uds/uds_dtc.h`) decodes a DTC's Powertrain / Chassis /
Body / Network class from its top two bits — no store needed.

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
```

The response is `59 02 <statusAvailabilityMask>` followed by one
`[DTC(3) statusOfDTC(1)]` record per matching DTC. Status `0x23` =
`testFailed | testFailedThisOperationCycle | testFailedSinceLastClear`, set by
`uds_dtc_store_report_test(..., true)`.

## See also

`../dtc_full_coverage` — every 0x19 sub-function, including the ones the
application formats itself and the 0x04/0x06 freeze-frame payloads.
