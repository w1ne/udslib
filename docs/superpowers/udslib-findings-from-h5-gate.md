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

## F-2 — built-in ReadDataByIdentifier (0x22) — EMULATOR (resolved T1)
**Observed (Task 0 spike):** the built-in RDBI DID-table path returned NRC 0x31
(requestOutOfRange); worked around with a hand-rolled user-service for 0x22.
**T1 investigation:** The DID table was configured correctly (verified: count=2,
entries[0].id=0xF190, entries[0].size=14, storage=pointer to VIN data). With a
correctly-wired DID table in `.rodata`, two distinct failure modes were observed:

  1. With `uds_ctx_t ctx` on stack (non-static): `uds_internal_handle_read_data_by_id`
     dispatched but returned NRC 0x31 — the udslib DID search path apparently
     did not match (ctx may have been misaligned / partially uninitialized at that
     stack offset; non-repro when ctx moved to static storage).
  2. With `static uds_ctx_t ctx`: ECU logged `ECU_SID_22` (dispatch confirmed) but
     produced no response — the CPU faulted inside `uds_internal_handle_read_data_by_id`
     at the `memcpy(tx_buf, entry->storage, entry->size)` call where `entry->storage`
     pointed to `.rodata` VIN data via a doubly-indirect `.rodata` pointer chain
     (`g_ecu_dids[]` in .rodata → `.storage` field value → VIN data in .rodata).

**Root cause:** The labwired STM32H563 FLASH emulator does not correctly model
multi-byte data reads from a `.rodata` address when that address is obtained through
two levels of indirection from another `.rodata` structure. The emulator faults
(Default_Handler / CPU exception, no UDS response emitted). This is the same fault
class as F-3.

**Note:** udslib itself is not defective. The built-in path works on real hardware
and in the host_sim test (where all data is in process heap/BSS). The fault is
specific to the labwired H563 FLASH model.

**Classification:** `EMULATOR` — labwired H563 double-indirection FLASH read fault.

**Workaround (in place, T1):** VIN and customer-name DID data are stored in RAM
(static char, initialized byte-by-byte in main()) instead of `.rodata`. A user-service
shim for SID 0x22 (`svc_rdbi`) reads from these RAM buffers and calls `uds_send_response`
directly, bypassing the built-in RDBI handler. Marked `/* WORKAROUND F-2 */` in code.

**Revert condition:** remove `svc_rdbi` and `g_user_services` once the labwired H563
FLASH emulator correctly handles double-indirection `.rodata` reads.

## F-3 — local `const char hex[]` arrays fault in ECU FLASH — EMULATOR (resolved T1)
**Observed (Task 0 spike):** local `const char hex[] = "0123…"` arrays in the ECU
handler caused Bus Read Faults; removed the hex-print diagnostics to proceed.
**T1 investigation:** Both linker scripts (`h5_uds_ecu_full/firmware/minimal.ld` and
the labwired-core `h563-uds-ecu/firmware/minimal.ld`) place `.rodata*` inside `.text`
in FLASH at the same LMA, so the fault is NOT a linker misconfiguration.

The root cause is the same emulator issue as F-2: the labwired H563 FLASH model
faults on certain access patterns when reading from `.rodata`. The specific failure
pattern for F-3 is indexed array access from a local `const char[]` (whose data is in
`.rodata`), while the emulator works for pointer-width reads and single-element reads
in certain access contexts.

**Classification:** `EMULATOR` — same labwired H563 FLASH access bug as F-2.

**Workaround (in place, T1):** All diagnostic hex printing uses `static const char`
lookup tables (kept in `.rodata` but accessed via direct static reference, not via
double-indirection pointer). The `g_ecu_vin` and `g_customer_name` DID data are in
RAM (see F-2 workaround). The bug surfaces whenever `.rodata` data is read through a
pointer stored in another `.rodata` structure.

**Revert condition:** same as F-2 — once the labwired H563 FLASH emulator is fixed.

---

## Hand-rolled workarounds currently in the tree (Task 1) — REVERT when emulator fixed

1. **ECU user-service for SID 0x22** (`svc_rdbi` + `g_user_services[]` in
   `examples/h5_uds_ecu_full/firmware/main.c`) — reads VIN/customer-name from RAM
   buffers instead of the built-in DID-table path. → Revert once labwired H563
   double-indirection FLASH reads work. See F-2.

2. **DID data in RAM** (`g_ecu_vin`, `g_customer_name` as non-const static, initialized
   byte-by-byte) — prevents the memcpy-from-.rodata fault in svc_rdbi. → Revert to
   `static const char g_ecu_vin[] = "UDSLIB_SIM_001"` etc. once emulator is fixed.
   See F-2 / F-3.

3. **F-1 tester workaround from Task 0 spike** — tester bypasses `uds_client_request`
   and calls `uds_isotp_send` directly. → Revert in **T2**: use the real
   `uds_client_request` + `uds_response_cb`. If it genuinely fails, record as BUG and
   keep a `/* WORKAROUND udslib F-1 */`-marked shim.

## How to add an entry (for implementers)
When you hit a udslib problem: add a section above with the status tag, the exact
observation (file:line, request/response bytes), your independent check, and — only
if you must proceed — a workaround marked in code with `/* WORKAROUND udslib F-N: … */`
so it is greppable and revertible.
