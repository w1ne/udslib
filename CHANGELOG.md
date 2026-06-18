# Changelog

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
