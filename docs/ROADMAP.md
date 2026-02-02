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
- [x] **Concurrency**: Multi-threaded validation suite.
- [x] **Fuzzing**: LibFuzzer integration for deep packet inspection.

## Phase 9: Full ISO 14229-1 Compliance Alignment (Complete)
- [x] **ISO-TP (ISO 15765-2)**: STmin & Block Size (BS) enforcement timers.
- [x] **ISO-TP (ISO 15765-2)**: Wait Frame (FS=WT) & Overflow handling.
- [x] **Core (ISO 14229-1)**: Aligned NRC priorities with Figure 10 (Subfunction check priority).
- [x] **Concurrency**: Explicit request blocking (NRC 0x21) even during `UDS_PENDING`.
- [x] **Service Hardening**: 0x3D echoing, 0x22 multi-read overflow protection.
- [x] **Security**: Non-deterministic hook-based seed/key exchange.

## Phase 10: Deep Inspection & Ecosystem (Planned)
- [x] **Fuzzing**: Integrated LibFuzzer/AFL harness for SDU parsing robustness.
- [x] **Advanced Security**: Support for SID 0x29 (Authentication) state persistence.
- [ ] **More OS Ports**: Native support for QNX and PikeOS.
- [ ] **Python ODX-to-C V2**: Enhanced routine/DID parameter generator.

## Future Goals
- **Interoperability**: Automated testing against major commercial stacks.
- **Certification**: Preparation for ISO 26262 and ASPICE compliance.
