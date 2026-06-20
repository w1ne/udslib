# udslib findings from the H5 all-services gate (Sub-project B)

Running log of udslib (and emulator) issues discovered while building the dual-H5
all-services tester gate. **Workarounds here are temporary** — fixes land later.
Each entry: what was observed, independent assessment, where the real fix belongs,
and the marked workaround (if any) so it can be reverted once fixed.

Status legend: `BUG` (confirmed udslib defect) · `MISUSE` (firmware was wrong, no
udslib fix) · `EMULATOR` (labwired-side) · `NEEDS-CONFIRM` (not yet pinned).

---

## F-1 — `uds_client_request` response handling — NEEDS-CONFIRM
**Observed (Task 0 spike):** implementer reported `uds_client_request` "passes len=0
via fn_tp_send" and bypassed it by calling `uds_isotp_send` directly and manually
setting `ctx.client_pending_sid` / `ctx.client_cb`.
**Independent check:** the SEND side is correct — `src/core/uds_core.c:597` sends
`fn_tp_send(ctx, ctx->config->tx_buffer, (uint16_t)(len + 1u))` with
`tx_buffer[0]=sid` and the payload copied after it. It does NOT send len=0. So the
"len=0" claim is a misdiagnosis on the send path.
**Real open question:** does the core route an inbound positive/negative RESPONSE to
`client_cb` (matching `client_pending_sid`), or does it treat inbound frames as
server requests? The spike only got responses after manually wiring the callback —
which *may* indicate the client RESPONSE-dispatch path is the actual gap.
**Action:** Task 2 MUST call the real `uds_client_request` + a real `uds_response_cb`
and feed responses via `uds_isotp_rx_callback`. If the cb is never invoked on a real
response, capture the exact path here and reclassify as `BUG`. If it works, this was
`MISUSE`. No workaround should survive into T2 without an entry here justifying it.

## F-2 — built-in ReadDataByIdentifier (0x22) returned NRC 0x31 — likely MISUSE
**Observed (Task 0 spike):** the built-in RDBI DID-table path returned NRC 0x31
(requestOutOfRange); worked around with a hand-rolled user-service for 0x22.
**Independent check:** Sub-project A's `h563-uds-ecu` (commit history on
labwired-core) uses the built-in `uds_did_table_t` and returns a correct
`62 F1 90 …` positive response. So the built-in path works when configured right.
**Assessment:** almost certainly DID-table misconfiguration in the spike ECU (wrong
entry shape / size / id), not a udslib defect.
**Action:** Task 1 ECU uses the `examples/host_sim` + `h563-uds-ecu` DID-table
config and the built-in 0x22 path. If it still returns 0x31 with a verified-correct
table, reclassify as `BUG` and record the table here.

## F-3 — local `const char hex[]` arrays fault in ECU FLASH — NEEDS-CONFIRM (likely EMULATOR or linker)
**Observed (Task 0 spike):** local `const char hex[] = "0123…"` arrays in the ECU
handler caused Bus Read Faults; removed the hex-print diagnostics to proceed.
**Independent check:** `h563-uds-ecu` uses `static const` arrays and string literals
without faulting, so `.rodata` access broadly works. A *local* `const` array is
copied from `.rodata` (LMA in FLASH) to stack on each call; a fault there points to
either the example's linker `.rodata` LMA placement or the labwired H5 FLASH/rodata
model — NOT udslib core.
**Action:** during Task 1, determine whether it's a linker-script fix (in the udslib
example) or a labwired emulator issue; record the resolution here. If emulator,
open a labwired-core finding instead.

---

## Hand-rolled workarounds currently in the tree (commit 06f86d5, Task 0 spike) — REVERT these

These bypass the real udslib APIs that B exists to exercise. They are tracked here so
they are reverted (not inherited) as the firmwares are built out in T1/T2:

1. **Tester bypasses `uds_client_request`** — calls `uds_isotp_send` directly and
   hand-sets `ctx.client_pending_sid` / `ctx.client_cb`. → Revert in **T2**: use the
   real `uds_client_request` + `uds_response_cb` (see F-1). If it genuinely fails,
   record it as a BUG and keep a `/* WORKAROUND udslib F-1 */`-marked shim.
2. **ECU hand-rolls a user-service for SID 0x22** instead of the built-in DID-table
   RDBI. → Revert in **T1**: use the built-in `uds_did_table_t` path like
   `h563-uds-ecu` (see F-2).
3. **ECU hex-print diagnostics removed** to dodge the FLASH `.rodata` fault. → Not a
   correctness workaround; resolve F-3 (linker vs emulator) in T1, then diagnostics
   can return if wanted.

A B gate that ships on top of #1/#2 would be testing hand-rolled glue, not udslib —
the same "theater" failure class we already corrected once in Sub-project A. Each
surviving workaround MUST have a matching `F-N` entry above with a real justification.

## How to add an entry (for implementers)
When you hit a udslib problem: add a section above with the status tag, the exact
observation (file:line, request/response bytes), your independent check, and — only
if you must proceed — a workaround marked in code with `/* WORKAROUND udslib F-N: … */`
so it is greppable and revertible.
