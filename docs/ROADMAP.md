# LibUDS Roadmap

## Phase 1: Symmetric Core & Integration (Complete)
- [x] Basic Service Dispatcher (SID 0x10)
- [x] Dependency Injection Architecture
- [x] ISO-TP Fallback Layer (Multi-frame FF/CF/FC support)
- [x] Symmetric Client/Server API (`uds_client_request`)
- [x] "Spliced" Transport Abstraction (Native vs Fallback)
- [x] Host Simulation Harness (UDP Virtual CAN)
- [x] Automated Integration Testing (C & Python)

## Phase 2: Platform & Ecosystem (Complete)
- [x] **Zephyr OS Integration**: Module structure, CMake, Kconfig integration.
- [x] **Zephyr Shared Transport**: Wrapper for native `subsys/canbus/isotp`.
- [x] **External Validation**: Integrated `driftregion/iso14229` and `py-uds`.
- [x] **Unit Testing Hardening**: Full CMocka test suite implementation.

## Phase 3: Industrial Hardening (Complete)
- [x] **Modular Service Architecture**: Table-driven dispatcher and decoupled service handlers.
- [x] **Full ISO-14229-1 Compliance**: NRC priorities and rigid session management.
- [x] **Read/Write Data (0x22/0x2E)**: High-level table-driven handlers.
- [x] **State Persistence (NVM)**: Hooks for saving/restoring session and security states across resets.
- [x] **Safety Gate Callback**: Integrated check (`fn_is_safe`) before executing destructive services (0x11, 0x2E, 0x31).
- [x] **Thread-Safe Core**: Mutex/critical-section abstractions for RTOS safety (OSAL).

## Phase 4: Modern Cybersecurity (Complete)
- [x] **Authentication (SID 0x29)**: Support for certificate-based exchange (ISO 21434).
- [x] **Advanced Security Levels**: Formalized multi-level security state machine.

## Phase 5: Flash Engine & Memory Services (Complete)
- [x] **Download/Transfer Services (0x34, 0x36, 0x37)**: Core logic for firmware data transfer.
- [x] **Memory Services (0x23, 0x3D)**: Read/Write Memory By Address with bounds checking.
- [x] **Routine Control (0x31)**: Extensible routine execution framework.

## Phase 6: Tooling & Automation (Future)
- [ ] **ODX-to-C Generator**: Automated generation of DID tables and Service registries from ODX/PDX files.
- [ ] **Python Test Generator**: Scaffolding integration tests based on the diagnostic database.

## Long Term Vision
- **Golden Standard Connectivity**: Plug-and-play testing against any major commercial UDS stack.
- **Safety Certification**: Preparing the codebase for pre-certified ISO 26262/ASPICE components.
