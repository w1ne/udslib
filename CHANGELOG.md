# Changelog

## [1.11.0] - 2026-02-04

### Added
- **Copyright Headers**: Added standard license headers to all source, include, test, and script files.
- **License**: Switched to `PolyForm-Noncommercial-1.0.0` for all core library components.

### Fixed
- **CI/CD**: Resolved issue in release workflow where `coverage_summary.txt` was not generated.

## [1.10.0] - 2026-02-04

### Added
- **Service 0x2A (ReadDataByPeriodicIdentifier)**: Integrated scheduler supporting Fast, Medium, and Slow rates.
- **Service 0x2F (InputOutputControlByIdentifier)**: Full support for actuator control with security and session validation.
- **Service 0x35 (RequestUpload)**: Symmetrical data provider flow for ECU memory upload.
- **TransferData (0x36) Robustness**: Added `transfer_accept_last_block_replay` configuration to gracefully handle lost positive responses.

### Changed
- Updated internal dispatcher to support subfunction-less services with manual validation (0x2F).

### Fixed
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
