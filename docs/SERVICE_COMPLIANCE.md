# ISO 14229-1 (UDS) Compliance

Single source of truth for what LibUDS supports against the standard. Every row
below is grounded in the dispatcher's service table (`src/core/uds_core.c`).

- **Standard:** ISO 14229-1:2013, with selected 2020 additions (e.g. 0x29 Authentication).
- **Transport:** ISO 15765-2 (ISO-TP) over CAN and CAN-FD.
- **Role:** Server (ECU) stack. A generic client requester (`uds_client_request`) is also provided.
- **Coverage:** **27 of 27** application services implemented.
- **Last updated:** 2026-06-19.

## Implemented services (27)

| SID | Service | Notes |
| :--- | :--- | :--- |
| 0x10 | DiagnosticSessionControl | Default / Programming / Extended / SafetySystem; returns configured P2/P2*; re-locks security on transition; optional `fn_session_transition_allowed` hook for OEM transition graphs. |
| 0x11 | ECUReset | Hard, Soft, KeyOffOn; SuppressPosMsg supported. |
| 0x14 | ClearDiagnosticInformation | Optional memory-selection byte. |
| 0x19 | ReadDTCInformation | DTCStatusMask validation; library-formatted wire layout for 0x01/0x02/0x0A (via `fn_dtc_list`) and 0x04/0x06 framing (via `fn_dtc_snapshot`/`fn_dtc_extdata`); raw `fn_dtc_read` fallback. Severity/WWH-OBD subfunctions: 0x07 reportNumberOfDTCBySeverityMaskRecord, 0x08 reportDTCBySeverityMaskRecord, 0x09 reportSeverityInformationOfDTC, 0x14 reportDTCFaultDetectionCounter, 0x42 reportWWHOBDDTCByMaskRecord, 0x55 reportWWHOBDDTCWithPermanentStatus (via `fn_dtc_list` + `dtc_severity_availability_mask`). |
| 0x22 | ReadDataByIdentifier | Multi-DID; per-DID session/security gating; tx-buffer overflow protection. |
| 0x23 | ReadMemoryByAddress | ALFID parsing + bounds/length checks. |
| 0x24 | ReadScalingDataByIdentifier | Library frames `0x64 <DID>`; scalingByte/scalingData via `fn_read_scaling`. |
| 0x27 | SecurityAccess | App seed/key callbacks; requestSeed→sendKey sequencing (NRC 0x24); attempt counter + delay (NRC 0x36/0x37). |
| 0x28 | CommunicationControl | Subfunctions 0x00–0x05 + validation. |
| 0x29 | Authentication | Certificate exchange (ISO 14229-1:2020). |
| 0x2A | ReadDataByPeriodicIdentifier | Integrated scheduler (Fast / Medium / Slow / Stop). |
| 0x2C | DynamicallyDefineDataIdentifier | defineByIdentifier / defineByMemoryAddress / clear; bookkeeping via `fn_dynamic_did`. |
| 0x2E | WriteDataByIdentifier | Table-driven registry; per-DID session/security gating. |
| 0x2F | InputOutputControlByIdentifier | Actuator control; subfunction-less validation. |
| 0x31 | RoutineControl | Start / Stop / RequestResults. |
| 0x34 | RequestDownload | ALFID-based address/size parsing. |
| 0x35 | RequestUpload | Symmetrical data-provider flow. |
| 0x36 | TransferData | Block-sequence-counter tracking + rollover; optional last-block replay. |
| 0x37 | RequestTransferExit | Completion logic. |
| 0x38 | RequestFileTransfer | modeOfOperation (1–5) + path-length validation; operation via `fn_file_transfer`. |
| 0x3D | WriteMemoryByAddress | Echoes address/size in the positive response. |
| 0x3E | TesterPresent | Busy-relaxed NRC 0x21 handling; suppressed TP keeps S3 alive. |
| 0x83 | AccessTimingParameter | Read / set / default the live P2 and P2* timing. |
| 0x84 | SecuredDataTransmission | Library owns 0x84 framing + Administrative Parameter + secured-session gate; crypto via `fn_secure_decode`/`fn_secure_encode` hooks (no bundled cipher). |
| 0x85 | ControlDTCSetting | DTC ON/OFF; SuppressPosMsg. |
| 0x86 | ResponseOnEvent | onDTCStatusChange (0x01) / onChangeOfDataIdentifier (0x03) / onTimerInterrupt (0x02) + start/stop/clear/report; app events emitted via `uds_roe_trigger`, timer events from `uds_process`. Only onComparisonOfValues (0x07) deferred (NRC 0x12). |
| 0x87 | LinkControl | Baud-rate transition; verify→transition handshake (NRC 0x24 if out of sequence). |

## Coverage notes

All 27 ISO 14229-1 application services are now dispatched. Partial-depth items
worth noting: 0x86 implements 7 of 8 sub-functions (only onComparisonOfValues
0x07 returns NRC 0x12); 0x84 ships the protocol envelope and
delegates the cipher to the integrator. Unknown/unsupported SIDs are rejected
with NRC 0x11 (serviceNotSupported).

## Transport conformance (ISO 15765-2)

- Frame types: Single (SF), First (FF), Consecutive (CF), Flow Control (FC).
- Classic CAN (8-byte) and CAN-FD (up to 64-byte, DLC-aligned) framing.
- Flow control: BlockSize and STmin honored; STmin 0x00–0x7F (ms) and 0xF1–0xF9 (sub-ms) decoded.
- Timeouts: **N_Cr** (reception) and **N_Bs** (flow-control wait) abort a stalled transfer instead of wedging the engine (configurable per instance; default 1000 ms).
- Short/malformed frames (truncated SF/FF/FC, NULL/zero-length) are rejected.

## Conformance safeguards

- **NRC priority:** Session → Subfunction → Length → Security → Safety, per ISO 14229-1.
- **Session policy:** by default every service is reachable in every session (the integrator enforces policy via `fn_is_safe` or per-service `session_mask`). Set `config.restrict_sessions` for a built-in ISO-sensible default: reprogramming services (0x34/0x35/0x36/0x37/0x87/0x3D) → programming session; other privileged services (0x27/0x11/0x28/0x29/0x2E/0x2F/0x31/0x83/0x85/0x23) → extended or programming. Disallowed-session requests return NRC 0x7F.
- **SecurityAccess:** seed cached and passed to the key verifier; key requires a prior requestSeed at the same level (else NRC 0x24); configurable attempt limit and delay timer.
- **Session/security reset:** security level and outstanding seed cleared on session change and S3 timeout.
- **Session transitions:** by default any ISO-valid session ($01 default / $02 programming / $03 extended / $04 safetySystem) may be entered from any session, per ISO 14229-1 (the standard imposes no transition graph). OEM-specific graphs (e.g. requiring extended before programming) are enforced by supplying the optional `fn_session_transition_allowed(ctx, from, to)` hook; returning false rejects the request with NRC 0x22.
- **RCRRP limit:** configurable cap on NRC 0x78 (ResponsePending) repetitions to prevent infinite loops.
- **Safety gates:** application callback can block destructive services (Reset, Write, Download) with NRC 0x22.
- **Async:** `UDS_PENDING` (NRC 0x78) supports non-blocking integration with slow flash/hardware.
- **MISRA-C:2012:** checked in CI via cppcheck's addon; no mandatory-rule violations. Required/advisory deviations documented in [docs/MISRA.md](MISRA.md).

## Verification

- End-to-end reprogramming reference (session → security → link/timing → download → transfer → checksum), smoke-tested in CI: `examples/pro_flash_tool/`.
- Full server request/response sequence over ISO-TP (all services above): `tests/integration/test_uds.py`.
- Unit suite per service under `tests/unit/` (run via `ctest`).
- PCAP + HTML session-report demo (SessionControl + ReadDataByIdentifier/VIN): `run_capture.sh`.

## Remediation history

A 2026-02 audit against ISO 14229-1:2013 identified 20 deviations (C-01…C-20)
in the then-implemented services — invalid-session acceptance, NRC-priority
ordering, ALFID validation, security reset, RCRRP looping, DID buffer bounds,
response echoing, and others. **All C-01…C-20 were remediated in v1.9.0.**

Since then: per-DID session gating corrected (programming/extended were
swapped — v1.12.0), SecurityAccess requestSeed→sendKey sequencing added
(v1.12.0), and ISO-TP N_Cr/N_Bs timeouts added (v1.12.0).
