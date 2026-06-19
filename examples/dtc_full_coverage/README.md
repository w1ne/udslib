# DTC full-coverage example

Shows an application covering **every** `ReadDTCInformation` (0x19) sub-function,
split by who formats the response:

- **Library-framed** — the library owns the ISO 14229-1 wire layout and the
  application only supplies records via `fn_dtc_list` (and the freeze-frame
  payloads via `fn_dtc_snapshot` / `fn_dtc_extdata`).
- **Application-served** — the memory-region, by-record-number, emissions-OBD,
  and user-defined-memory variants need a parameter that does not generalise
  into one hook, so the library routes them to the raw `fn_dtc_read` fallback,
  which receives the full request so the handler can read what it needs.

Everything is static (no `malloc`/`free`), matching the library's
zero-allocation, MISRA-friendly design.

## How the two paths are wired

```c
cfg.fn_dtc_list     = app_dtc_list;     /* 0x01/0x02/0x0A, severity, first/most-recent, ... */
cfg.fn_dtc_snapshot = app_dtc_snapshot; /* 0x04 freeze-frame payload                        */
cfg.fn_dtc_extdata  = app_dtc_extdata;  /* 0x06 extended-data payload                        */
cfg.fn_dtc_read     = app_dtc_read;     /* 0x03,0x05,0x0F-0x13,0x16-0x19 (app-formatted)     */
```

For an application-served sub-function the library writes `[0x59, sub]` and the
handler writes the payload that follows. `fn_dtc_read` receives the request so
it can read the parameters — e.g. the status mask, DTC, record number, or
memory selection:

```c
static int app_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req,
                        uint16_t req_len, uint8_t *out_buf, uint16_t max_len)
{
    switch (subfn) {
        case 0x0Fu: { /* reportMirrorMemoryDTCByStatusMask */
            if (req_len < 3u) { return -0x13; }   /* incorrectMessageLength */
            uint8_t status_mask = req[2];         /* parameter from the request */
            /* ... write statusAvailabilityMask + matching {DTC(3) status(1)} ... */
        }
        /* ... 0x03, 0x05, 0x10-0x13, 0x16-0x19 ... */
    }
}
```

Snapshot (0x04) and extended-data (0x06) content is manufacturer-specific, so
the library leaves it to the application: `app_dtc_snapshot` returns a
timestamp, supply voltage, and power mode; `app_dtc_extdata` returns the
fault-occurrence / pending / aged / ageing counters.

## Build & run

```sh
make run
```

Expected output:

```
=== Library-framed sub-function (0x02 reportDTCByStatusMask) ===
  0x02 reportDTCByStatusMask                   -> 59 02 7F 01 23 45 09 C0 01 00 08

=== Snapshot / extended-data (via fn_dtc_snapshot / fn_dtc_extdata) ===
  0x04 reportDTCSnapshotRecordByDTCNumber      -> 59 04 01 23 45 09 01 02 10 01 19 06 13 0D 05 2A 10 02 8C 02
  0x06 reportDTCExtendedDataRecordByDTCNumber  -> 59 06 01 23 45 09 01 03 01 00 28

=== Application-served sub-functions (via fn_dtc_read) ===
  0x03 reportDTCSnapshotIdentification         -> 59 03 01 23 45 01
  0x05 reportDTCStoredDataByRecordNumber       -> 59 05 01 01 23 45 09
  0x0F reportMirrorMemoryDTCByStatusMask       -> 59 0F 7F 10 10 10 09 20 20 20 08
  0x10 reportMirrorMemoryDTCExtDataRecord      -> 59 10 10 10 10 09 01 2A
  0x11 reportNumberOfMirrorMemoryDTC           -> 59 11 7F 01 00 02
  0x12 reportNumberOfEmissionsOBDDTC           -> 59 12 7F 01 00 01
  0x13 reportEmissionsOBDDTCByStatusMask       -> 59 13 7F 01 23 45 09
  0x16 reportDTCExtDataRecordByRecordNumber    -> 59 16 01 01 23 45 09 2A
  0x17 reportUserDefMemoryDTCByStatusMask      -> 59 17 01 7F 10 10 10 09 20 20 20 08
  0x18 reportUserDefMemoryDTCSnapshotRecord    -> 59 18 01 10 10 10 09 01 2A
  0x19 reportUserDefMemoryDTCExtDataRecord     -> 59 19 01 10 10 10 09 01 2A

All sub-functions answered positively.
```

In the 0x04 record, `19 06 13 0D 05 2A` is the timestamp (2025-06-19 13:05:42),
`8C` the voltage (14.0 V), `02` the power mode. In 0x06, `03 01 00 28` are the
occurrence / pending / aged / ageing counters.

## See also

`../dtc_store` — the opt-in reference store that implements the library-framed
sub-functions for you.
