# udslib findings from the H5 all-services gate (Sub-project B)

Running log of udslib (and emulator) issues discovered while building the dual-H5
all-services tester gate. **Workarounds here are temporary** — fixes land later.
Each entry: what was observed, independent assessment, where the real fix belongs,
and the marked workaround (if any) so it can be reverted once fixed.

Status legend: `BUG` (confirmed udslib defect) · `MISUSE` (firmware was wrong, no
udslib fix) · `EMULATOR` (labwired-side) · `NEEDS-CONFIRM` (not yet pinned).

---

## F-1 — `uds_client_request` send fails on the tester — CONFIRMED EMULATOR BUG

**Status: EMULATOR (confirmed by instrumentation).** The labwired STM32H563 Cortex-M33
emulator drops r2 (the third argument) when dispatching a 3-argument function pointer
call through a struct field (indirect/BLX call).

**Confirmed evidence (Task 3 instrumentation):**
- Tester client `uds_config_t` was verified correct: `tx_buffer = g_tx_buf` (256-byte
  RAM array), `tx_buffer_size = 256`, `fn_tp_send = isotp_send_adapter`. All guards in
  `uds_client_request` (src/core/uds_core.c:576-585) pass.
- `uds_client_request` returns `CLIENT_RC=00` (UDS_OK) — it does NOT early-return; it
  reaches the `fn_tp_send(ctx, data, len)` indirect call at line 600.
- `isotp_send_adapter` is called (the indirect dispatch DOES reach the callee) but
  receives `TP_SEND_LEN=0000` — r2 is zeroed by the emulator before the indirect branch
  resolves. `uds_isotp_send` receives `len=0` and sends nothing; the ECU times out.
- Direct BL calls (e.g., `uds_isotp_send` called explicitly) pass r2 correctly, which
  is why the workaround works.

**Why the ECU is not affected:** the ECU's `fn_tp_send` IS also called indirectly
(via `uds_send_response` → `ctx->config->fn_tp_send`), but the ECU's `isotp_send_adapter`
receives valid `len` in that path. The difference is call-site ABI context — the server-
side dispatch in `uds_process` likely uses a different register allocation than the client-
side dispatch in `uds_client_request`. The net effect is that the tester client path is
affected; the ECU server path is not. Both use the same adapter and the same struct field.

**udslib itself is bug-free.** The send-path logic at src/core/uds_core.c:573-607 is
correct; host_sim tests pass. The response-dispatch path (src/core/uds_core.c:651-667)
is also correct and is NOT bypassed by the workaround.

**Revert condition:** remove the workaround once the labwired H563 emulator correctly
passes r2 on all indirect 3-arg calls (open a labwired-core issue with the BLX/r2 repro:
`fn_tp_send` via `uds_config_t.fn_tp_send` receives len=0 in the tester client path).

**Workaround (in place):** bypass `uds_client_request` for the SEND step only.
Set `ctx.client_pending_sid` and `ctx.client_cb` directly (same as `uds_client_request`
does), build the SDU in a local buffer, and call `uds_isotp_send` directly (BL, not BLX).
The response-dispatch path (`uds_input_sdu_addr` checking `client_pending_sid` → firing
`client_cb`) is used unchanged.  Marked `/* WORKAROUND udslib F-1 */` in
`examples/h5_uds_tester/firmware/main.c`.

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

## F-2/F-3 — CONTROLLER ASSESSMENT (the "EMULATOR" classification is NOT yet confirmed)
The "double-indirection `.rodata` read fault" diagnosis is architecturally suspect and
must not be treated as a confirmed labwired bug until a minimal repro pins it:
- **Counter-evidence:** the firmware's `uart_puts(const char*)` reads `.rodata` string
  data via a pointer and works. The emulator models *load instructions*, not
  "indirection levels"; if single `.rodata`-via-pointer reads work and reading a
  pointer-width value from `.rodata` works, the combined memcpy should too.
- **More likely real cause:** a `uds_did_entry_t` STRUCT-LAYOUT mismatch — the
  firmware's initializer vs the CURRENT udslib header (the sibling `uds_service_entry_t`
  recently gained a 7th field `address_mode`; if `uds_did_entry_t` shifted similarly,
  `.storage` is read from the wrong offset → garbage pointer → fault). That is a
  firmware/version bug, fixable in the example, NOT an emulator defect.
- **Action (deferred, do NOT block B):** a focused repro — (a) print
  `sizeof(uds_did_entry_t)` and `offsetof(.storage)` from the firmware and compare to
  the current header; (b) try the built-in path with the DID struct + VIN in RAM vs
  `.rodata` to isolate whether it's layout or genuinely the FLASH model. Only if (a)
  matches and (b) still faults from `.rodata` is this a real labwired H563 FLASH bug
  (then open a labwired-core finding). Until then this stays NEEDS-CONFIRM, not EMULATOR.

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

3. **F-1 tester workaround (T3 confirmed, permanent until emulator fixed)** — tester
   bypasses the `fn_tp_send` indirect call in `uds_client_request` by setting
   `ctx.client_pending_sid` / `ctx.client_cb` directly and calling `uds_isotp_send`
   directly.  The response-dispatch path (uds_input_sdu_addr → client_cb) is intact.
   Marked `/* WORKAROUND udslib F-1 */` in `examples/h5_uds_tester/firmware/main.c`.
   Instrumentation confirmed: `CLIENT_RC=00` (config correct, all guards pass),
   `TP_SEND_LEN=0000` (r2 zeroed by emulator before BLX resolves).
   Revert once labwired H563 emulator correctly passes r2 on indirect 3-arg calls.

## How to add an entry (for implementers)
When you hit a udslib problem: add a section above with the status tag, the exact
observation (file:line, request/response bytes), your independent check, and — only
if you must proceed — a workaround marked in code with `/* WORKAROUND udslib F-N: … */`
so it is greppable and revertible.
