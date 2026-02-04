# Roadmap

For current implemented service coverage, see `docs/SERVICE_COMPLIANCE.md`.

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
- [x] **Concurrency**: Multi-threaded validation suite.
- [x] **Fuzzing**: LibFuzzer integration for deep packet inspection.

## Phase 9: Full ISO 14229-1 Compliance Alignment (Complete)
- [x] **ISO-TP (ISO 15765-2)**: STmin & Block Size (BS) enforcement timers.
- [x] **ISO-TP (ISO 15765-2)**: Wait Frame (FS=WT) & Overflow handling.
- [x] **ISO-TP (ISO 15765-2)**: CAN-FD Support (Frames > 8 bytes, Up to 64 bytes).
- [x] **Core (ISO 14229-1)**: Aligned NRC priorities with Figure 10 (Subfunction check priority).
- [x] **Concurrency**: Explicit request blocking (NRC 0x21) even during `UDS_PENDING`.
- [x] **Service Hardening**: 0x3D echoing, 0x22 multi-read overflow protection.
- [x] **Security**: Non-deterministic hook-based seed/key exchange.

## Phase 10: Deep Inspection & Ecosystem (Planned)
- [x] **Fuzzing**: Integrated LibFuzzer/AFL harness for SDU parsing robustness.
- [x] **Advanced Security**: Support for SID 0x29 (Authentication) state persistence.
- [ ] **More OS Ports**: Native support for QNX and PikeOS.
- [ ] **Python ODX-to-C V2**: Enhanced routine/DID parameter generator.

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

- [x] **Security Enhancements**: Improve 0x27 robustness.
  - Multiple security levels (0x01-0x7F) support
  - Attempt counter and lockout mechanism
  - Delay timer after failed attempts

- [x] **Protocol Hardening (Compliance Fixes)**:
  - [x] **C-01/C-02**: Fix Session 0x10 state machine (invalid sessions, Programming session)
  - [x] **C-03**: Fix 0x11 Reset to support SuppressBit
  - [x] **C-04/C-05**: Fix Core Dispatcher (Security vs Length priority, missing NRCs)
  - [x] **C-06**: Fix Security Reset on session transition
  - [x] **C-07**: Implement RCRRP limit for NRC 0x78
  - [x] **C-08/C-09**: Fix Memory Services (0x34 len, 0x23/0x3D ALFID check)
  - [x] **C-10**: Fix 0x19 API (add DTCStatusMask support)
  - [x] **C-11**: Fix 0x28 Min Length (reject len < 3)
  - [x] **C-12**: Fix 0x22 Buffer Overflow (add bounds check)
  - [x] **C-13**: Fix 0x36 Sequence Check (verify rollover)
  - [x] **C-14**: Fix 0x27 Delay Timer (implement lockout)
  - [x] **C-15**: Implement Addressing Mode checks (Functional vs Physical)
  - [x] **C-16**: Fix 0x85 Group Filtering (parse optional data)
  - [x] **C-17**: Fix Async Race Condition (busy handling)
  - [x] **C-18**: Implement usage Granularity for DIDs (Security/Session per DID)
  - [x] **C-19**: Fix 0x10 Timing Response (use config values)
  - [x] **C-20**: Fix 0x3D Response Echo (echo Addr/Size)

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

## Phase 11b: Embedded ECU Update Workflow (Planned)
**Target**: Provide a reference-grade, production-friendly flashing workflow on top of 0x31/0x34/0x36/0x37.

- [ ] **Transfer Integrity**: Rolling checksum/CRC across 0x36 blocks.
  - CRC32 accumulator updated per received block
  - Optional final verification step on 0x37 (or a dedicated 0x31 routine)

- [ ] **Block Handling**: Robust 0x36 block sequence counter (BSC) policy.
  - Accept exact expected BSC; optionally accept last-BSC replay for retransmissions
  - Consistent NRC mapping (e.g., 0x73 for wrong BSC, 0x72 for programming failure)
  - Optional config flag to ACK a repeated last-BSC without re-processing data (interoperability)

- [ ] **Block Size Strategy (CAN-FD)**: Optimize transfer payload sizes for CAN-FD (up to 64-byte frames).
  - Default “large block” streaming mode for fast links
  - Configurable max block size to match flash page/buffer constraints

- [ ] **Memory Region Policy**: First-class support for multiple programmable regions (e.g., boot/app/staging).
  - Region allowlist with bounds checks
  - Region-specific erase/write alignment requirements

- [ ] **Update Lifecycle**: Optional “verify + activate” flow.
  - Store update flags/state in app-provided NVM
  - Post-download checksum verification
  - Optional A/B activation and reboot policy hooks

- [ ] **Embedded Super-Loop Profile**: Reference integration for a 1ms tick loop.
  - Guidance for calling `uds_process()` and transport processing from a periodic tick
  - Recommended strategy for feeding frames from ISR → queue → UDS task/loop

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

## Phase 13: Parity with Commercial Stacks (Active)
**Target**: Implement missing services identified

- [ ] **Service 0x2F (InputOutputControlByIdentifier)**: Actuator control.
- [ ] **Service 0x35 (RequestUpload)**: Symmetrical data provider flow.
- [ ] **Service 0x2A (ReadDataByPeriodicIdentifier)**: Core scheduler integration.

## Future Goals
- **Interoperability**: Automated testing against major commercial stacks.
- **Certification**: Preparation for ISO 26262 and ASPICE compliance.
- **Service Coverage**: 26/26 services (100% ISO 14229-1 compliance).
