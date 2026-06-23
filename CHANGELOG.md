# Changelog

## [Unreleased]

## [2.0.0] - 2026-06-23

Architecture release: response emission, suppression, and reset ordering move
into the dispatch framework, and the runtime context is regrouped. All ISO
14229 conformance from 1.20.0 is preserved; this release changes the **handler
API**, not the wire behaviour.

It also lands an architecture-hardening pass: a real two-context concurrency
model (an RX/input context running concurrently with `uds_process`), buffer-bound
hardening on every response path, configuration single-source-of-truth, and new
configurability. The wire behaviour is unchanged and no public function signature
changed; two integrator-visible behaviours are refined (see Changed).

### Breaking
- **Service handlers use a result-descriptor contract.** A handler registered
  via `config.user_services` is now
  `void handler(uds_ctx_t*, const uint8_t*, uint16_t, uds_result_t *out)`: it
  writes the positive payload into `tx_buffer` and describes the outcome via
  `uds_ok(out, len)` / `uds_nrc(out, nrc)` / `uds_pending(out)` /
  `uds_none(out)` instead of calling `uds_send_response` / `uds_send_nrc` /
  returning `UDS_PENDING`. The framework (`execute_handler`) is the single
  authority for emission, suppressPosRsp, and the deferred ECUReset.
  `uds_send_response` / `uds_send_nrc` remain public for application code that
  emits outside a handler. Migration: `return uds_send_response(ctx, n);` →
  `uds_ok(out, n);`; `return uds_send_nrc(ctx, sid, nrc);` → `uds_nrc(out, nrc);`;
  `return UDS_PENDING;` → `uds_pending(out);`.
- **Runtime `uds_ctx_t` fields regrouped into sub-structs** (not API): fields
  now live under `session` / `security` / `server` / `client` / `scratch`
  (e.g. `ctx->active_session` → `ctx->session.active`, `ctx->security_level` →
  `ctx->security.level`). Affects only code that read `uds_ctx_t` fields
  directly. The per-dispatch `scratch` is reset at the start of every top-level
  request, so no per-request flag can leak into the next.

- **Client role moved to `uds_client_ctx_t`** (`uds/uds_client.h`): the server
  `uds_ctx_t` no longer carries client state or auto-routes responses.
  `uds_client_request` takes a `uds_client_ctx_t*`; feed incoming responses to
  `uds_client_handle_response` (returns true if consumed). `uds_response_cb`'s
  first argument is now `uds_client_ctx_t*`.

### Added
- **ISO-TP generic SDU sink** `uds_isotp_set_sdu_handler(iso, fn, cookie)`:
  reassembled SDUs are delivered to a custom handler instead of the server
  `uds_input_sdu`, so one ISO-TP channel can feed a UDS server or a UDS client.
  Default (no handler) preserves server delivery.
- **ECUReset (0x11) — wait for the response to be on the wire before resetting**: a new optional `fn_tx_complete(ctx)` config hook lets the library hold the reset until the positive `0x51` response has physically left the transport (TX mailbox drained), not merely been queued. It is bounded by the new `reset_tx_wait_ms` budget (`0` = `UDS_DEFAULT_RESET_TX_WAIT_MS`, 50 ms) so a stuck transport can never hang the ECU; when the hook is unset, behaviour is unchanged (immediate reset). This closes a real-hardware race: a `fn_reset` that calls `NVIC_SystemReset()` could reboot before the FDCAN frame was arbitrated onto the bus, so a tester saw silence after `11 01` and only `11 81` (suppressPosRsp) appeared to work. The H5 example now polls FDCAN `TXBRP` in `fn_tx_complete` and performs a real `NVIC_SystemReset`. (#88)
- **Configurable S3 session timeout** via `uds_config_t.s3_ms` (`0` =
  `UDS_S3_TIMEOUT_MS`, 5000 ms). Previously the S3 revert was a fixed
  compile-time constant.
- **ISO-TP receiver Block Size / STmin setters** `uds_tp_isotp_set_block_size()`
  and `uds_tp_isotp_set_st_min()` let the receiver advertise its own BS/STmin in
  the Flow Control frame (defaults unchanged: BS 8, STmin 0).
- **`uds_init` validates buffer sizes**: a `tx_buffer_size` or `rx_buffer_size`
  below `UDS_MIN_TX_BUFFER_SIZE` / `UDS_MIN_RX_BUFFER_SIZE` (8) is rejected with
  `UDS_ERR_INVALID_ARG` instead of risking an overflow later.
- **Two-context concurrency (OSAL)**: with the mutex hooks supplied, an RX/input
  context (`uds_input_sdu` / ISO-TP RX) may run concurrently with the
  `uds_process` context. Cross-context state is `volatile`-qualified and
  `fn_tp_send` runs outside the critical section. The mutex must be an ISR-safe
  critical section if RX runs in an interrupt (see OSAL.md); the `freertos_demo`
  example is the canonical two-context reference.

### Changed
- A handler that returns without describing a result now fails closed
  (generalReject 0x10) instead of emitting whatever is in `tx_buffer`.
- The deferred ECUReset is owned by the framework: it runs strictly after the
  response is emitted, never during a captured 0x84/0x86 inner dispatch, is
  cancelled if the response fails to encode or send (the tester gets an NRC /
  nothing, so the ECU does not reboot), and never fires from a captured
  ResponseOnEvent dispatch.
- **P2/P2\* resolved once at `uds_init`** (single source of truth) from
  `p2_ms`/`p2_star_ms`, with the legacy `p2_server_max`/`p2_star_server_max`
  pair folded in. The 0x10 response derives its advertised timing from the
  resolved values; mutating the P2 config fields *after* `uds_init` no longer
  changes the advertised timing.
- **`fn_tp_send`'s `data` may be a private snapshot**, not a pointer into
  `config->tx_buffer`, for responses ≤ `UDS_TX_FLUSH_SNAPSHOT_MAX` (512 bytes);
  larger frames are sent from `tx_buffer` under the lock. Applications must use
  the `data`/`len` given and must not assume `data == tx_buffer`.

### Fixed
- **Bounded every response-buffer copy**: ResponseOnEvent setup echo, the async
  ResponseOnEvent (0xC6) emit, and the periodic (0x2A) scheduler now check
  `tx_buffer_size` before writing — an oversized one is rejected (responseTooLong)
  or skipped instead of overflowing `tx_buffer`.
- **Concurrency correctness** (found and fixed under ThreadSanitizer): a staged
  responsePending (0x78) is no longer clobbered by a periodic/ROE frame on the
  same `uds_process` tick; the post-TX action (ECUReset/LinkControl) flag
  bookkeeping is serialized under the lock so a deferred reset can no longer race
  its own cancellation; and the client request path snapshots its frame like the
  server path. The transport call and the reset/link callbacks remain outside the
  lock.

### Known limitations
- SecuredDataTransmission (0x84) shares `config->tx_buffer` with its copied-out
  inner dispatch; this is safe because a dispatch is serialized under the OSAL
  lock (the two-context model protects whole-request dispatch, not arbitrary
  reentrancy).
- The handler-result contract is the stable surface; the `uds_ctx_t` field
  layout is not part of the API contract and may evolve.
- The test suite is host-simulation based (mocked transport + virtual clock).

## [1.21.0] - 2026-06-23

### Added
- **ECUReset (0x11) — wait for the response to be on the wire before resetting**: a new optional `fn_tx_complete(ctx)` config hook lets the library hold the reset until the positive `0x51` response has physically left the transport (TX mailbox drained), not merely been queued. It is bounded by the new `reset_tx_wait_ms` budget (`0` = `UDS_DEFAULT_RESET_TX_WAIT_MS`, 50 ms) so a stuck transport can never hang the ECU; when the hook is unset, behaviour is unchanged (immediate reset). This closes a real-hardware race: a `fn_reset` that calls `NVIC_SystemReset()` could reboot before the FDCAN frame was arbitrated onto the bus, so a tester saw silence after `11 01` and only `11 81` (suppressPosRsp) appeared to work. The H5 example now polls FDCAN `TXBRP` in `fn_tx_complete` and performs a real `NVIC_SystemReset`. (#88)

## [1.20.0] - 2026-06-22

### Added
- **ISO-TP configurable frame padding**: `uds_tp_isotp_set_pad_byte()` sets the byte used to fill unused bytes in every transmitted frame (SF, FF, CF, FC). The default is now `0xCC` (`ISOTP_PAD_BYTE_DEFAULT`), the value ISO 15765-2:2016 recommends to minimize stuff-bit insertions on the wire; override per channel (e.g. `0xAA`, or `0x00` to restore the previous fill). (#67)
- **ISO-TP physical/functional addressing**: a channel can now recognise a functional (broadcast) RX ID via `uds_tp_isotp_set_functional_id()`; requests are tagged physical/functional and delivered through the new `uds_input_sdu_addr()` (`uds_input_sdu()` stays as a physical-default wrapper). Services gate on a new `address_mode` field in `uds_service_entry_t` (0 = both). Functional addressing is Single-Frame only, and functionally addressed requests suppress NRC 0x11/0x12/0x7E/0x7F/0x31 per ISO 14229-1. Completes #42.
- **ISO-TP full-duplex mode**: `uds_tp_isotp_set_mode(iso, ISOTP_FULL_DUPLEX)` lets a segmented reception and a segmented transmission proceed simultaneously on the same N_AI; an inbound frame no longer aborts an in-flight multi-frame response. Default remains half-duplex (prior behavior). The ISO-TP context now keeps independent RX/TX state. Scope: one concurrent RX and one TX per channel — two simultaneous transmissions on one N_AI are not supported (single TX connection per N_AI, per ISO 15765-2). (#42)
- **ReadDTCInformation (0x19) — severity, fault-detection counter, and WWH-OBD subfunctions**: added reportNumberOfDTCBySeverityMaskRecord (0x07), reportDTCBySeverityMaskRecord (0x08), reportSeverityInformationOfDTC (0x09), reportDTCFaultDetectionCounter (0x14), reportWWHOBDDTCByMaskRecord (0x42), and reportWWHOBDDTCWithPermanentStatus (0x55). The DTC record now carries severity, functional unit, fault-detection counter, aging counter, and functional group; all new subfunctions are formatted by the library from records supplied via the existing `fn_dtc_list` hook. New config field `dtc_severity_availability_mask`. (#39)
- **DTC classification helpers** (`uds/uds_dtc.h`): `uds_dtc_category()` decodes Powertrain/Chassis/Body/Network from a DTC, plus named `UDS_DTC_STATUS_*`, `UDS_DTC_SEVERITY_*`, and `UDS_DTC_FGID_*` constants. (#39)
- **Optional reference DTC store** (`uds/uds_dtc_store.h`): opt-in, application-provides-storage helper that manages DTC instances (register/get/clear), tracks the fault-detection counter and aging (with self-heal), and supplies ready-made `fn_dtc_list`/`fn_dtc_extdata`/`fn_dtc_clear` callbacks. The protocol core does not depend on it. (#39)
- **ReadDTCInformation (0x19) — complete sub-function coverage**: added reportFirstTestFailedDTC (0x0B), reportFirstConfirmedDTC (0x0C), reportMostRecentTestFailedDTC (0x0D), reportMostRecentConfirmedDTC (0x0E), and reportDTCWithPermanentStatus (0x15), each formatted by the library from records supplied via the existing `fn_dtc_list` hook (first/most-recent select by occurrence order; permanent is reported as the confirmed set). The 0x19 sub-function mask now admits every standard sub-function (0x01–0x19) plus 0x42/0x55; the memory-region, by-record-number, emissions-OBD, and user-defined-memory variants (0x03, 0x05, 0x0F–0x13, 0x16–0x19) are routed to the raw `fn_dtc_read` hook for the application to format rather than rejected with subFunctionNotSupported. (#39)
- **DiagnosticSessionControl (0x10) — safetySystemDiagnosticSession ($04)**: the fourth standard ISO 14229-1 session type is now accepted (sub-function mask widened to $01–$04) and mapped to a new `UDS_SESSION_SAFETY` service-gate bit. (#40)
- **Optional session-transition policy hook** `fn_session_transition_allowed(ctx, from, to)`: called before 0x10 changes the active session. Returning false rejects the request with NRC 0x22 (conditionsNotCorrect) and leaves the session unchanged. When unset (default), any session may be entered from any session — matching ISO 14229-1, which imposes no transition graph. Lets integrators enforce OEM-specific transition graphs (e.g. extended-before-programming) without baking a non-standard default into the library. (#40)
- **ResponseOnEvent (0x86) — onComparisonOfValues (0x07)** completes 0x86 sub-function coverage (8/8). The application reports an observed value via `uds_roe_trigger(ctx, 0x07, value)` and the library emits when it satisfies the stored comparison. The 0x07 `eventTypeRecord` is interpreted pragmatically as `<comparisonOperator(1)><referenceValue(4)>` (operators 0x01 equal / 0x02 greater-than / 0x03 less-than), documented in code as a simplification pending a concrete spec/use-case.
- **ResponseOnEvent persistence helpers** `uds_roe_serialize()` / `uds_roe_deserialize()`: write/restore the stored 0x86 event definitions to a caller-owned buffer so an application can persist them across a reset using its own NVM. The library stays storage-agnostic (no collision with the `fn_nvm_save` session/security channel); restored events are inactive until a startResponseOnEvent.
- **Authentication (0x29) — sub-function validation, state, and native handling**: a sub-function mask now admits only 0x00–0x08 (else NRC 0x12). `deAuthenticate` (0x00) and `authenticationConfiguration` (0x08, returns the new `cfg.auth_configuration` byte) are handled natively. A new `ctx.authenticated` flag (set by the application's `fn_auth` on success) is auto-cleared on deAuthenticate, session change, S3 timeout, and reset — mirroring `security_level`. The certificate/proof/challenge sub-functions (0x01–0x07) remain delegated to `fn_auth` (crypto stays in the app/HSM).
- **Authentication-gated services**: a new optional `cfg.fn_auth_required(ctx, sid)` hook lets the application require an authenticated channel for selected services; when it returns true and `ctx.authenticated` is false, the request is rejected with NRC 0x34 (authenticationRequired). `examples/auth_challenge` demonstrates the full flow (gated service rejected → cert verify → challenge → proof → gated service allowed).
- **CommunicationControl (0x28) — enhanced-address sub-functions completed**: the 2-byte nodeIdentificationNumber carried by enableRxAndDisableTxWithEnhancedAddressInformation (0x04) and enableRxAndTxWithEnhancedAddressInformation (0x05) is now parsed and delivered to `fn_comm_control`; these sub-functions are length-checked (require ≥ 5 bytes, else NRC 0x13). New `uds_comm_type_t` enum names the communicationType message classes (normal / network-management / both), and `uds_comm_control_type_t` gains `UDS_COMM_ENABLE_RX_DISABLE_TX_ENH` (0x04) and `UDS_COMM_ENABLE_RX_TX_ENH` (0x05). (#55)

### Changed
- **`fn_comm_control` gains a `node_id` parameter** (BREAKING): the CommunicationControl (0x28) callback is now `int (*)(uds_ctx_t *, uint8_t ctrl_type, uint8_t comm_type, uint16_t node_id)`. `node_id` carries the nodeIdentificationNumber for the enhanced-address sub-functions (0x04/0x05) and is 0 for 0x00–0x03. Implementers of `fn_comm_control` must update their signature. (#55)
- **`fn_dtc_read` now receives the request payload**: the optional ReadDTCInformation (0x19) raw-fallback hook signature gains `const uint8_t *req, uint16_t req_len` before the output buffer, so applications serving the sub-functions the library does not frame (0x03, 0x05, 0x0F–0x13, 0x16–0x19) can read the request's status/severity mask, DTC, record number, or memory selection. Implementers of `fn_dtc_read` must update their signature. (#39)
- **`uds_auth_type_t` renumbered to ISO 14229-1:2020** (BREAKING): the Authentication (0x29) sub-function enum was off by one (`deAuthenticate` was 0x01). It now matches the standard — `UDS_AUTH_DEAUTHENTICATE = 0x00`, `VERIFY_CERT_UNI = 0x01`, `VERIFY_CERT_BI = 0x02`, `PROOF_OF_OWNERSHIP = 0x03`, `TRANSMIT_CERT = 0x04`, `REQUEST_CHALLENGE = 0x05` (was `REQUEST_TOKEN = 0x06`), plus new `VERIFY_PROOF_UNI = 0x06`, `VERIFY_PROOF_BI = 0x07`, `CONFIGURATION = 0x08`. Update any code/wire that used the old values.
- **ISO-TP frame padding default is now `0xCC`** (was `0x00`): transmitted SF/FF/CF/FC frames pad unused bytes with `0xCC` per ISO 15765-2:2016 instead of `0x00`. This changes only the on-wire fill of otherwise-identical frames; protocol behavior is unchanged. Call `uds_tp_isotp_set_pad_byte(&iso, 0x00)` to restore the previous fill. (#67)

### Fixed
- **ECUReset (0x11) reset hook could fire before a secured response was sent**: building on the send-before-reset ordering fix (#76), when a 0x11 is wrapped in a SecuredDataTransmission (0x84) the inner `0x51` is only captured — the response the tester receives is the outer secured frame, sent later. `fn_reset` is now deferred until that outer frame is on the wire, and is skipped entirely if the response cannot be sent (or securing fails). A captured ResponseOnEvent (0x86) inner dispatch can never trigger a reset. New `reset_pending` context state. (#76)
- **ECUReset enableRapidPowerShutDown (0x11 sub 0x04) was incomplete**: the sub-function was rejected by the 0x11 sub-function mask (which only admitted 0x01–0x03), and even when reached the response omitted the mandatory `powerDownTime` byte. The mask now admits 0x01–0x05, and the 0x04 positive response is `{0x51, 0x04, powerDownTime}` sourced from the new `cfg.power_down_time`. `uds_reset_type_t` gains `UDS_RESET_ENABLE_RAPID_SHUTDOWN` (0x04) and `UDS_RESET_DISABLE_RAPID_SHUTDOWN` (0x05). (ISO 14229-1)
- **TransferData (0x36) wrong block sequence returned NRC 0x24 instead of 0x73**: a mismatched blockSequenceCounter now returns `wrongBlockSequenceCounter (0x73)` per ISO 14229-1, distinct from `requestSequenceError (0x24)`.
- **TransferData (0x36) accepted before RequestDownload/Upload**: a transfer must now be armed by a successful RequestDownload (0x34) or RequestUpload (0x35); TransferData outside an active transfer is rejected with `requestSequenceError (0x24)` and never reaches `fn_transfer_data`. The transfer is disarmed by RequestTransferExit (0x37) and aborted on S3 session timeout. New `transfer_active` context state.
- **Responses exceeding the TX buffer were dropped silently**: `uds_send_response()` returned an internal error and sent nothing when a response was larger than `tx_buffer_size`, leaving the tester to time out. It now emits `responseTooLong (NRC 0x14)` for on-the-wire responses (captured inner 0x84/0x86 responses still surface the internal error to their wrapper).

## [1.19.0] - 2026-06-19

### Added
- **ResponseOnEvent (0x86) — onTimerInterrupt (0x02)**: a started timer event emits its stored serviceToRespondTo response periodically from `uds_process()` at the configured rate (eventTypeRecord byte: 0x01 slow / 0x02 medium / 0x03 fast), honouring the event window. Now 7 of 8 ROE sub-functions are implemented; only onComparisonOfValues (0x07) remains deferred (NRC 0x12). The timer and trigger paths share one emit helper.

## [1.18.0] - 2026-06-19

### Added
- **ResponseOnEvent (0x86)** — completes ISO 14229-1 service coverage (**27/27**). A stateful event engine: setup (0x01 onDTCStatusChange / 0x03 onChangeOfDataIdentifier) stores an event definition with its serviceToRespondTo; start (0x05) / stop (0x00) / clear (0x06) manage activation; reportActivatedEvents (0x04) lists active events. The application notifies the stack of real-world changes via the new `uds_roe_trigger(ctx, event_type, param)` API; for each active matching definition the library runs the stored service and emits its response as a `0xC6` message. Event windows expire from `uds_process()`. Storage is a fixed slot array (`UDS_ROE_MAX_EVENTS`, default 4; set to 0 to compile the feature out). onTimerInterrupt (0x02) and onComparisonOfValues (0x07) return NRC 0x12 (deferred); stored events are volatile (no NVM persistence yet).

## [1.17.0] - 2026-06-19

### Added
- **ReadScalingDataByIdentifier (0x24)**: the library frames the `0x64 <DID>` response and delegates the scalingByte/scalingData payload to the new `fn_read_scaling` callback. Returns NRC 0x31 when no reader is configured.
- **RequestFileTransfer (0x38)**: the library validates the modeOfOperation (1–5) and the filePathAndName length, then delegates the operation to the new `fn_file_transfer` callback, framing the `0x78 <modeOfOperation>` response prefix.
- **DynamicallyDefineDataIdentifier (0x2C)**: the library validates the sub-function (0x01 defineByIdentifier / 0x02 defineByMemoryAddress / 0x03 clear) and frames the `0x6C` response (echoing the defined DID when present); the definition is recorded/cleared by the new `fn_dynamic_did` callback.

## [1.16.0] - 2026-06-19

### Added
- **SecuredDataTransmission (0x84) + secured session**: the library owns the 0x84 framing and the Administrative Parameter, unwraps the secured payload via the new `fn_secure_decode` hook, dispatches the inner request with a `UDS_SESSION_SECURED` gate (so a service whose `session_mask` is exactly `UDS_SESSION_SECURED` is reachable only through 0x84), then secures the inner response via `fn_secure_encode` and wraps it as `0xC4`. Rejects nested 0x84 (NRC 0x31), honors inner suppress-positive-response, and surfaces hook-reported NRCs (e.g. a failed MAC). Crypto is application-supplied via the two hooks; a bundled reference cipher behind `UDS_ENABLE_BUILTIN_CRYPTO` is planned as a follow-up.

## [1.15.0] - 2026-06-19

### Added
- **Structured ReadDTCInformation (0x19)**: the library now formats the ISO 14229-1 wire layout for `reportNumberOfDTCByStatusMask` (0x01), `reportDTCByStatusMask` (0x02), and `reportSupportedDTC` (0x0A) from application-supplied records via the new `fn_dtc_list` callback (plus `dtc_status_availability_mask` and `dtc_format_id` config fields). `reportDTCSnapshotRecordByDTCNumber` (0x04) and `reportDTCExtendedDataRecordByDTCNumber` (0x06) are request-parsed and framed by the library, with the record payload supplied via `fn_dtc_snapshot` / `fn_dtc_extdata`. The library enforces `ResponseTooLong` (0x14) on overflow. The legacy raw `fn_dtc_read` path is retained as a fallback, so existing configs are unchanged.

## [1.14.0] - 2026-06-16

### Added
- **ISO-TP escape FirstFrame (ISO 15765-2)**: multi-frame transfers with `FF_DL > 4095` are now sent and received using the escape sequence (`10 00` + 4-byte length) instead of being rejected. Supports SDUs up to the 16-bit reassembly-buffer limit; a FirstFrame whose `FF_DL` exceeds the receive buffer is answered with `FC.OVFLW` rather than dropped silently.
- **ISO-TP FlowControl status handling**: the sender now honours `FlowStatus = WAIT` (keep waiting, restart N_Bs) and `OVFLW` (abort), and aborts on a reserved/invalid FS.

## [1.13.0] - 2026-06-15

### Added
- **LinkControl (0x87)** and **AccessTimingParameter (0x83)** — completes the reprogramming-negotiation flow; **22 of 27** services now implemented.
- **Opt-in session policy** (`config.restrict_sessions`): when enabled, reprogramming services are gated to the programming session and other privileged services to extended/programming. Default off (behavior unchanged).
- **End-to-end reprogramming example** (`examples/pro_flash_tool/`): a flash tool driving a LibUDS ECU through the full session → security → link/timing → erase → download → transfer → checksum flow; smoke-tested in CI.
- **Real fuzzers** for both untrusted-input boundaries (the core SDU dispatcher and the ISO-TP frame parser).

### Fixed
- **Periodic-scheduler NULL dereference**: ReadDataByPeriodicIdentifier (0x2A) registered an entry even with no `fn_periodic_read` configured, which `uds_process()` then called as a NULL pointer (found by the new fuzzer). The handler now returns NRC 0x22 and the scheduler is guarded.

### CI / Quality
- Pinned the Zephyr build to release **v4.4.1** (was `main`) for a deterministic gate.
- **MISRA-C:2012** enforced via cppcheck's addon against a documented deviation baseline (no mandatory-rule violations); see `docs/MISRA.md`.

### Docs
- Consolidated the two diverging compliance documents into a single, accurate `SERVICE_COMPLIANCE.md` (22/27 matrix + ISO 15765-2 transport conformance).
- Freshened the integrator guides for the instance-based ISO-TP API and the new features.

## [1.12.0] - 2026-06-15

### Added
- **ISO-TP transfer timeouts**: N_Cr (reception) and N_Bs (transmission) deadlines abort a stalled multi-frame transfer instead of wedging the engine; configurable per instance.
- **SecurityAccess sequence enforcement (0x27)**: sendKey without a preceding requestSeed is rejected with NRC 0x24; the issued seed is now passed to the key verifier.

### Changed
- **BREAKING (transport)**: the bundled ISO-TP layer is now fully instance-based. `uds_tp_isotp_init/set_fd/process`, `uds_isotp_send`, and `uds_isotp_rx_callback` take an explicit `uds_isotp_ctx_t *` and a caller-provided TX buffer (no file-global state); multiple channels can run concurrently. Wire `fn_tp_send` to a small adapter that forwards to the instance.

### Fixed
- **Mutex deadlock**: `uds_process` held the lock on the RCRRP-limit abort path.
- **Client/server state**: a finished asynchronous server request could swallow a later request as a stale client response; pending state is now split into server/client fields.
- **DID session gating**: per-DID programming and extended sessions were mapped to each other's bits (access-control flaw in 0x22/0x2E).
- **Periodic scheduler (0x2A)**: deadline comparison is now wrap-safe across the 32-bit millisecond rollover.
- **ISO-TP framing**: reject short SF/FF/FC frames and NULL/zero-length payloads.
- **Zephyr**: example and module now build (added missing `uds_service_io.c`; completed the instance-based transport migration).

### CI
- Fixed `docker_run.sh`, which ran `bash -c "bash"` and left static-analysis, build-posix, and build-zephyr as silent no-ops.
- Upgraded the toolchain to Ubuntu 24.04 (Python 3.12) with Zephyr SDK 1.0.1; made the coverage script lcov 2.x compatible.

## [1.10.0] - 2026-02-04

### Added
- **Copyright Headers**: Added standard license headers to all source, include, test, and script files.
- **License**: Switched to `PolyForm-Noncommercial-1.0.0` for all core library components.
- **Service 0x2A (ReadDataByPeriodicIdentifier)**: Integrated scheduler supporting Fast, Medium, and Slow rates.
- **Service 0x2F (InputOutputControlByIdentifier)**: Full support for actuator control with security and session validation.
- **Service 0x35 (RequestUpload)**: Symmetrical data provider flow for ECU memory upload.
- **TransferData (0x36) Robustness**: Added `transfer_accept_last_block_replay` configuration to gracefully handle lost positive responses.
- **CAN-FD Support**: Native support for 64-byte frames and DLC alignment in ISO-TP layer.

### Changed
- Updated internal dispatcher to support subfunction-less services with manual validation (0x2F).

### Fixed
- **CI/CD**: Improved release workflow reliability by generating coverage summaries.
- Improved unit test coverage for memory and flash services.
- Corrected ISO-TP frame padding handling in integration tests.

All notable changes to the UDSLib project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.9.0] - 2026-02-02

### Added
- **ISO 14229-1 Compliance Hardening**: Remediated 20 critical deviations (C-01 to C-20) identified in official audit.
- **RCRRP Limit (C-07)**: Implemented configurable limit for NRC 0x78 (Response Pending) repetitions in the core dispatcher to prevent infinite ECU loops.
- **Security Access Hardening (C-14, C-15)**: Added configurable security delay timers and attempt counters (NRC 0x36/0x37) with high-precision timing hooks.
- **ALFID Validation (C-08, C-09)**: Strict AddressAndLengthFormatIdentifier parsing for Memory (0x23/0x3D) and Flash (0x34) services. Invalid 0nd-nibble requests now return NRC 0x31.
- **DTC Management (0x19, 0x85)**: Implemented `DTCStatusMask` validation and filtering, and added `SuppressPosMsg` support for DTC setting control.
- **Response Echoing (C-20)**: Corrected SID 0x3D (WriteMemoryByAddress) response to include Address and Size parameters as per standard.
- **Flash Sequence Safety (C-13)**: Hardened SID 0x36 (TransferData) block sequence counter tracking and rollover validation.

### Fixed
- **Session Transition Safety (C-01, C-06)**: Fixed invalid session rejection and enforced automatic security reset on all session changes.
- **Timing Accuracy (C-19)**: Aligned internal timing responses with application configuration instead of hardcoded defaults.
- **NRC Priorities (C-04, C-11)**: Corrected service-specific NRC priorities (Session -> Subfunction -> Length) for ECC (0x28) and Security (0x27).
- **Concurrency (C-17)**: Resolved race conditions in asynchronous service handlers returning `UDS_PENDING`.

## [1.8.0] - 2026-02-01

### Added
- **MISRA-C:2012 Compliance**: Strictly audited the core library for baseline compliance (Rules 10.x, 17.x, 21.x).
- **Compliance Tooling**: Added `scripts/check_misra.sh` for automated release-blocking verification.
- **Protocol Constants**: Replaced all "magic numbers" with descriptive defines in `uds_internal.h`.
- **Hardened Dispatcher**: NRC priority logic now uses named bitmasks and validated subfunction tables (aligned with ISO 14229-1 Figure 10).
- **Hardened DID Management**: Protected SID 0x22 (ReadDataByIdentifier) against buffer overflows during multi-DID reads and added specific NRC propagation.
- **Flexible Data Transfer (0x34)**: Implemented standard `addressAndLengthFormatIdentifier` parsing for Request Download.
- **Compliance Verification**: New automated test suite (`test_compliance_pass*`) covering edge cases in response echoing, suppression, and timing safety.

### Changed
- **Branding**: Shifted from "Pro" suffix to standard semantic versioning (v1.8.0 Hardened Edition).
- **Core Safety**: Enforced zero standard library dependencies (`malloc`, `free`, `printf`) across the stack.
- **Type Safety**: Unified explicit typing for all bitwise and arithmetic operations in core services.
- **Timing State Machine**: Enforced minimum P2 (5ms) and P2* (100ms) timeouts in `uds_init` to prevent race conditions.
- **TesterPresent Behavior**: Relaxed Busy (NRC 0x21) logic to allow suppressed `TesterPresent` (0x3E) while other diagnostic operations are pending.

### Fixed
- **Session ID vs Bitmask**: Resolved ambiguity between UDS session IDs and internal bitmasks in the service registry.
- **NVM Persistence**: Restored state saving logic in session and security handlers.
- **Dispatcher Edge Cases**: Corrected subfunction mask validation for ReadDTCInfo (0x19) and fixed SID 0x3D echoing.

---

## [1.4.0] - 2026-01-31

### Added
- **Asynchronous Service Support**: Enabled non-blocking service handlers via `UDS_PENDING` return code, automatically managing NRC 0x78 (Response Pending) and P2* timing.
- **Enhanced Communication Control (0x28)**: Added application-level callbacks to validate and react to communication state changes.
- **Safety Gate Verification**: Formalized `fn_is_safe` logic to allow fine-grained access control before service execution.
- **Expanded Test Lifecycle**: Integrated end-to-end integration tests covering the full UDS lifecycle from session start to service execution in a loopback environment.
- **OS Examples**: Added reference implementations for Bare Metal, FreeRTOS, and Zephyr integration.

### Changed
- **Testing Standard**: Standardized on a Dockerized build/test environment for cross-platform consistency.
- **Documentation**: Comprehensive rewrite of the technical documentation to improve clarity and human readability.

### Fixed
- **Dispatcher Logic**: Resolved a regression where service handlers could be called even if the Safety Gate check failed.
- **NVM Persistence**: Corrected session and security state restoration logic during stack initialization.

## [1.3.0] - 2026-01-30

### Added
- **Memory Services**: SID 0x23 (Read Memory By Address) and 0x3D (Write Memory By Address) with address/length format parsing and bounds checking
- **OS Abstraction Layer (OSAL)**: Thread-safe architecture with mutex lock/unlock callbacks for RTOS integration
- **Authentication Service (0x29)**: Full ISO 14229-1:2020 certificate-based exchange support
- **Flash Engine**: Complete OTA support with SID 0x31 (Routine Control), 0x34 (Request Download), 0x36 (Transfer Data), and 0x37 (Request Transfer Exit)
- **DTC Management**: SID 0x14 (Clear DTC), 0x19 (Read DTC Info), and 0x85 (Control DTC Setting)
- **Doxygen Configuration**: API documentation generation support

### Changed
- **Core Architecture**: Refactored to table-driven service dispatcher with strict ISO 14229-1 NRC priority enforcement
- **Service Handlers**: Modularized into separate files under `src/services/` for maintainability
- **Test Infrastructure**: Expanded to 14 unit test suites with 100% coverage across all services

### Fixed
- **P2 Timing**: Corrected P2/P2* timer initialization in `uds_input_sdu`
- **Session Validation**: Fixed session mask conversion from session ID to bitmask
- **Safety Gate**: Restored missing validation checks in dispatch pipeline

## [1.1.0] - 2026-01-30

### Added
- **ECU Reset (0x11)**: Support for Hard, Soft, and KeyOffOn reset subfunctions with configurable application-side callbacks.
- **Communication Control (0x28)**: Support for enabling/disabling RX/TX states with internal protocol state tracking.
- **Doxygen Documentation**: Comprehensive API documentation for all public headers and internal core functions.
- **Unit Testing**: 8 formal unit test suites using the CMocka framework, covering all core services and timing edge cases.

### Changed
- **Architectural Cleanup**: Formalized naming conventions (all internal functions are now `static` and prefixed with `uds_internal_`).
- **Standardized Formatting**: Enforced industry-standard bracing and indentation across the entire C and Markdown codebase.
- **ISO-TP Fallback**: Aligned the internal ISO-TP transport layer with the new professional coding standards.

### Fixed
- **CMocka Dependency**: Resolved include order issues in the test suite where `<stdarg.h>` and `<setjmp.h>` were required before `<cmocka.h>`.
- **Compiler Warnings**: Fixed unused parameter warnings in the Tester Present (0x3E) unit tests.

### Removed
- Legacy ad-hoc logging and inconsistent error handling patterns.

## [1.0.0] - 2026-01-25
- Initial MVP Release.
- Basic UDS Core with SID 0x10, 0x22, 0x27, and 0x3E support.
- Zephyr OS integration and host simulator.
