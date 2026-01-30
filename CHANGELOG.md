# LibUDS Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.3.0] - 2026-01-30

### Added
- **Memory Services**: Implemented SID 0x23 (Read Memory By Address) and 0x3D (Write Memory By Address) with address/length format parsing and bounds checking
- **OS Abstraction Layer (OSAL)**: Thread-safe architecture with mutex lock/unlock callbacks for RTOS integration
- **Authentication Service (0x29)**: Full ISO 14229-1:2020 certificate-based exchange support
- **Flash Engine**: Complete OTA support with SID 0x31 (Routine Control), 0x34 (Request Download), 0x36 (Transfer Data), and 0x37 (Request Transfer Exit)
- **DTC Management**: SID 0x14 (Clear DTC), 0x19 (Read DTC Info), and 0x85 (Control DTC Setting)
- **Version Header**: Added `include/uds/uds_version.h` with semantic versioning macros
- **Doxygen Support**: Configuration file for API documentation generation
- **Enterprise Features**: Safety Gate callback, NVM persistence hooks, and comprehensive validation pipeline

### Changed
- **Core Architecture**: Refactored to table-driven service dispatcher with strict ISO 14229-1 NRC priority enforcement
- **Service Handlers**: Modularized into separate files under `src/services/` for maintainability
- **Test Infrastructure**: Expanded to 14 unit test suites with 100% coverage across all services
- **Integration Testing**: Enhanced Python test client with ISO-TP multi-frame support

### Fixed
- **P2 Timing**: Corrected P2/P2* timer initialization in `uds_input_sdu`
- **Session Validation**: Fixed session mask conversion from session ID to bitmask
- **Safety Gate**: Restored missing validation checks in dispatch pipeline
- **Transport Signature**: Updated all mock functions to match `uds_tp_send_fn` signature

### Documentation
- **ROADMAP.md**: Updated to reflect completion of Phases 3, 4, and 5
- **SERVICE_COMPLIANCE.md**: Marked all 15 implemented services as supported
- **README.md**: Added Enterprise Features section highlighting commercial readiness
- **COMMERCIAL_STRATEGY.md**: Defined "Industrial Middle Class" positioning
- **RELEASE_STRATEGY.md**: Established Gitflow workflow and CI/CD pipeline

### Testing
- **Unit Tests**: 100% pass rate (14/14 test suites)
- **Integration Tests**: Full service sequence verification via `test_uds.py`
- **Platform Coverage**: Validated on POSIX (host simulation) and Zephyr RTOS

### Compliance
- **ISO 14229-1**: 15 services fully implemented with strict standard adherence
- **Zero-Copy Design**: Optimized for low-footprint microcontrollers (no malloc)
- **Thread-Safety**: OSAL integration for concurrent access protection

---

## [Unreleased]

### Planned
- DoIP (ISO 13400) transport layer for Ethernet diagnostics
- ODX-to-C code generator for automated DID table generation
- MISRA-C compliance audit and certification preparation
- Safety certification roadmap (ISO 26262/ASPICE)

[1.0.0]: https://github.com/yourusername/libuds/releases/tag/v1.0.0
