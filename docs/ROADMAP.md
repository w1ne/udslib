# LibUDS Roadmap

This roadmap outlines the evolution of LibUDS from a core MVP to a production-ready industrial stack.

## Phase 1: Symmetric Core & Integration (Current)
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

## Phase 3: Industrial Hardening (In Progress)
- [x] **Security Access (SID 0x27)**: Mock implementation and verification.
- [x] **Maintenance Services (SID 0x11, 0x28)**: Formal support and tests.
- [x] **P2/P2* Timer Logic**: Automatic handling of "Response Pending" (0x78).
- [ ] **Full ISO-14229-1 Compliance**: NRC priorities and rigid session management.
- [ ] **Read/Write Data (0x22/0x2E)**: High-level table-driven handlers.

## Phase 4: Advanced Features (Planned)
- [ ] **Multi-Channel Support**: Multiple UDS contexts on different interfaces.
- [ ] **Automated Certification**: Compliance report generator for ISO-26262/ASPICE audits.

## Long Term Vision
- **Golden Standard Connectivity**: Plug-and-play testing against any major commercial or open-source UDS stack.
