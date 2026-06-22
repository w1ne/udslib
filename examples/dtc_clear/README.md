# Clear DTC (0x14) hook example

Self-contained example of the **ClearDiagnosticInformation (0x14)** config hook
requested in [issue #77](https://github.com/w1ne/udslib/issues/77):

```c
int (*fn_dtc_clear)(struct uds_ctx *ctx, uint32_t group);
```

No reference store is involved — the ECU keeps its own small DTC table and
clears it by hand, so the example shows exactly what the hook owns versus what
the library does for you.

## What the library does vs. what your hook does

The library handles the **wire format** for 0x14:

- rejects a malformed request (length `< 4`) with `0x7F 14 13` (incorrectMessageLength);
- replies `0x7F 14 22` (conditionsNotCorrect) automatically if no hook is registered;
- parses the 24-bit `groupOfDTC` argument and calls your hook;
- turns the result into a positive `0x54` response, or `0x7F 14 <NRC>` if you
  return a negative NRC.

Your hook only decides **which DTCs to drop** and **whether the preconditions
allow clearing**:

```c
static int ecu_clear_dtc(uds_ctx_t *ctx, uint32_t group)
{
    ecu_state_t *ecu = ctx->config->app_data;

    if (ecu->engine_running) {
        return -0x22;            /* ConditionsNotCorrect */
    }
    if (group == 0xFFFFFFu) {    /* clear all groups */
        clear_all(ecu);
        return UDS_OK;           /* -> positive 0x54 */
    }
    if (clear_one(ecu, group)) { /* clear a specific DTC group */
        return UDS_OK;
    }
    return -0x31;                /* RequestOutOfRange — no such group */
}
```

Wire it up in two lines of config:

```c
cfg.app_data     = &ecu;          /* recovered via ctx->config->app_data */
cfg.fn_dtc_clear = ecu_clear_dtc; /* ClearDiagnosticInformation (0x14) */
```

`group == 0xFFFFFF` is the ISO 14229-1 "clear all DTCs" argument; any other
value targets a specific DTC group.

## Build & run

```sh
make run
```

Expected output:

```
=== ClearDiagnosticInformation (0x14) ===
  clear all, engine running          -> 7F 14 22   (negative, NRC 0x22, 3 DTC(s) untouched)
  clear all, engine off              -> 54   (positive, 0 DTC(s) still active)
  clear DTC 0xDCBA98 (specific)      -> 54   (positive, 0 DTC(s) still active)
  clear DTC 0xAABBCC (unknown)       -> 7F 14 31   (negative, NRC 0x31, 0 DTC(s) untouched)

OK
```

`54` is the positive response (`0x14 + 0x40`). `7F 14 <NRC>` is a negative
response: `0x22` = conditionsNotCorrect (engine running), `0x31` =
requestOutOfRange (unknown group).

## See also

- `../dtc_store` — the **opt-in reference store** implements `fn_dtc_clear` for
  you (`uds_dtc_store_clear_cb`) so you don't write the table by hand.
- `../dtc_full_coverage` — every `ReadDTCInformation` (0x19) sub-function.
