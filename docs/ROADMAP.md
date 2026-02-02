# Roadmap

## Phase 1: Core Architecture (Complete)
- [x] Basic Service Dispatcher (SID 0x10).
- [x] Dependency Injection Architecture.
- [x] ISO-TP Fallback Layer.
- [x] Symmetric Client/Server API.
- [x] Transport Abstraction (Native vs Fallback).
- [x] Host Simulation Harness (UDP Virtual CAN).
- [x] Automated Integration Testing.

## Phase 2: Platform Integration (Complete)
- [x] **Zephyr OS**: Module structure, CMake, Kconfig.
- [x] **Shared Transport**: Wrapper for native `subsys/canbus/isotp`.
- [x] **Validation**: Integrated `driftregion/iso14229` and `py-uds`.
- [x] **Testing**: Full CMocka unit test suite.

## Phase 3: Industrial Hardening (Complete)
- [x] **Modular Services**: Table-driven dispatcher.
- [x] **Compliance**: Strict ISO-14229-1 NRC priorities and session management.
- [x] **Data Services**: High-level handlers for 0x22/0x2E.
- [x] **Persistence**: Hooks for saving session/security state (`fn_nvm`).
- [x] **Safety Gates**: Callback (`fn_is_safe`) for destructive services.
- [x] **Thread Safety**: OSAL mutex abstractions.

## Phase 4: Cybersecurity (Complete)
- [x] **Authentication (0x29)**: ISO 21434 certificate exchange support.
- [x] **Security Levels**: Multi-level state machine.

## Phase 5: Flash & Memory (Complete)
- [x] **Transfer Services**: 0x34, 0x36, 0x37 for firmware updates.
- [x] **Memory Services**: 0x23, 0x3D with bounds checking.
- [x] **Routine Control**: 0x31 framework.

## Phase 6: Tooling (Complete)
- [x] **ODX-to-C**: Generator for DID tables from ODX.
- [x] **Test Generator**: Python integration test scaffolding.

## Phase 7: Test Coverage & Robustness (Complete)
- [x] **Transport Layer**: ISO-TP segmentation/reassembly validation.
- [x] **Dispatcher**: Negative testing (NRCs) and fuzzing.
- [x] **NVM Persistence**: Core implementation and loopback verification.
- [x] **Integration Suite**: Full stack lifecycle tests (Dockerized).
- [x] **Safety**: Gate callback verification.

## Phase 8: Asynchronous Architecture & Hardening (Complete)
- [x] **Async Services**: Support `UDS_PENDING` return code for non-blocking handlers.
- [x] **Service 0x28**: Enhanced Communication Control with app callbacks.
- [ ] **Concurrency**: Multi-threaded validation suite.
- [ ] **Fuzzing**: LibFuzzer integration for deep packet inspection.

## Phase 9: Industrial Robustness & Compliance (Planned)
- [ ] **ISO-TP (ISO 15765-2)**: STmin & Block Size (BS) enforcement timers.
- [ ] **ISO-TP (ISO 15765-2)**: Wait Frame (FS=WT) & Overflow handling.
- [ ] **Core (ISO 14229-1)**: Align NRC priorities with Figure 10 (Subfunction check priority).
- [ ] **Concurrency**: Explicit request blocking during `UDS_PENDING` operations.
- [ ] **Security (0x27)**: CSRNG hook for secure, non-deterministic seeds.
- [ ] **Architecture**: Buffer scaling (support for multi-KB SDUs for flashing).

## Phase 10: High-Priority Service Expansion (ISO 14229-1 Compliance)
**Target**: Achieve 80%+ service coverage with critical diagnostic features.

- [ ] **Service 0x2A (ReadDataByPeriodicIdentifier)**: Periodic data streaming.
  - Sub-functions: sendAtSlowRate (0x01), sendAtMediumRate (0x02), sendAtFastRate (0x03), stopSending (0x04)
  - Periodic scheduler in `uds_process()` with configurable transmission rates
  - Active periodic identifier registry with automatic cleanup on session timeout
  - Conflict resolution with synchronous services

- [ ] **Service 0x2F (InputOutputControlByIdentifier)**: Actuator control and testing.
  - Sub-functions: returnControlToECU (0x00), resetToDefault (0x01), freezeCurrentState (0x02), shortTermAdjustment (0x03)
  - IO control state machine per identifier
  - Application callback for actuator control (`fn_io_control`)
  - Safety gate integration for dangerous actuator states
  - Timeout-based automatic return to ECU control

- [ ] **Enhanced NRC Coverage**: Add missing negative response codes.
  - 0x14: Response Too Long
  - 0x21: Busy Repeat Request
  - 0x24: Request Sequence Error
  - 0x31: Request Out Of Range
  - 0x72: General Programming Failure
  - 0x92-0x99: Voltage/temperature condition NRCs

- [ ] **Security Enhancements**: Improve 0x27 robustness.
  - Multiple security levels (0x01-0x7F) support
  - Attempt counter and lockout mechanism
  - Delay timer after failed attempts

- [ ] **Protocol Hardening (Compliance Fixes)**:
  - **C-01/C-02**: Fix Session 0x10 state machine (invalid sessions, Programming session)
  - **C-03**: Fix 0x11 Reset to support SuppressBit
  - **C-04/C-05**: Fix Core Dispatcher (Security vs Length priority, missing NRCs)
  - **C-06**: Fix Security Reset on session transition
  - **C-07**: Implement RCRRP limit for NRC 0x78
  - **C-08/C-09**: Fix Memory Services (0x34 len, 0x23/0x3D ALFID check)
  - **C-10**: Fix 0x19 API (add DTCStatusMask support)
  - **C-11**: Fix 0x28 Min Length (reject len < 3)
  - **C-12**: Fix 0x22 Buffer Overflow (add bounds check)
  - **C-13**: Fix 0x36 Sequence Check (verify rollover)
  - **C-14**: Fix 0x27 Delay Timer (implement lockout)
  - **C-15**: Implement Addressing Mode checks (Functional vs Physical)
  - **C-16**: Fix 0x85 Group Filtering (parse optional data)
  - **C-17**: Fix Async Race Condition (busy handling)
  - **C-18**: Implement usage Granularity for DIDs (Security/Session per DID)
  - **C-19**: Fix 0x10 Timing Response (use config values)
  - **C-20**: Fix 0x3D Response Echo (echo Addr/Size)

## Phase 11: Medium-Priority Service Expansion
**Target**: Achieve 90%+ service coverage with advanced diagnostic capabilities.

- [ ] **Service 0x24 (ReadScalingDataByIdentifier)**: Scaling/unit metadata retrieval.
  - Extend `uds_did_entry_t` with optional scaling metadata
  - Scaling formula encoding (linear, rational, tabular)
  - Unit identifiers (ISO 14229-1 Annex C)

- [ ] **Service 0x2C (DynamicallyDefineDataIdentifier)**: Runtime DID composition.
  - Sub-functions: defineByIdentifier (0x01), defineByMemoryAddress (0x02), clearDynamicallyDefinedDataIdentifier (0x03)
  - Dynamic DID registry (separate from static DID table)
  - Composite DID construction from multiple sources
  - Memory allocation for dynamic definitions

- [ ] **Service 0x35 (RequestUpload)**: ECU-to-tester data upload.
  - Mirror of 0x34 (RequestDownload) for upload direction
  - Upload state machine with 0x36/0x37 integration
  - Application callback for data source (`fn_upload_read`)

- [ ] **Service 0x86 (ResponseOnEvent)**: Event-driven diagnostic responses.
  - Sub-functions: stopResponseOnEvent (0x00), onDTCStatusChange (0x01), onTimerInterrupt (0x02), onChangeOfDataIdentifier (0x03), reportActivatedEvents (0x04)
  - Event registry and monitoring framework
  - Background evaluation in `uds_process()`
  - Service-to-respond-to execution engine

- [ ] **Enhanced Service 0x19 (ReadDTCInformation)**: Additional sub-functions.
  - 0x06: reportDTCExtDataRecordByDTCNumber
  - 0x10: reportMirrorMemoryDTCByStatusMask
  - 0x13: reportMirrorMemoryDTCExtDataRecordByDTCNumber

## Phase 12: Advanced & Niche Features
**Target**: Full ISO 14229-1 compliance (100% service coverage).

- [ ] **Service 0x83 (AccessTimingParameter)**: Runtime P2/P2* adjustment.
  - Read/write current timing parameters
  - Validation of timing parameter ranges
  - Persistence across sessions (optional)

- [ ] **Service 0x87 (LinkControl)**: Diagnostic link management.
  - Sub-functions: verifyBaudrateTransitionWithFixedBaudrate (0x01), verifyBaudrateTransitionWithSpecificBaudrate (0x02), transitionBaudrate (0x03)
  - Baud rate transition state machine
  - Transport layer reconfiguration hooks

- [ ] **Service 0x84 (SecuredDataTransmission)**: Encrypted diagnostic payloads.
  - Crypto library integration (mbedTLS or similar)
  - Encrypted request/response wrapper
  - Key management hooks

- [ ] **Service 0x38 (RequestFileTransfer)**: File system operations.
  - Sub-functions: addFile (0x01), deleteFile (0x02), replaceFile (0x03), readFile (0x04), readDir (0x05)
  - File system abstraction layer
  - Application callback for file operations

- [ ] **Additional Session Types**: Expand session support.
  - Safety System Session (0x04) for ISO 26262
  - Manufacturer-specific sessions (0x40-0x5F) framework

## Future Goals
- **Interoperability**: Automated testing against major commercial stacks.
- **Certification**: Preparation for ISO 26262 and ASPICE compliance.
- **Service Coverage**: 26/26 services (100% ISO 14229-1 compliance).

