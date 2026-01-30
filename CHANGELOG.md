# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-01-30

### Added
- **ECU Reset (0x11)**: Supported Hard, Soft, and KeyOffOn reset types with application callbacks.
- **Communication Control (0x28)**: Formal state tracking for RX/TX enable/disable subfunctions.
- **Table-Driven DID Registry**: Scalable system for Data Identifiers (DIDs) supporting RDBI (0x22) and WDBI (0x2E).
- **Expanded Test Suite**: Added 3 new unit test suites (`test_service_11.c`, `test_service_28.c`, `test_service_did.c`).
- **Enhanced host_sim**: Updated example to demonstrate DID table usage and ECU reset callbacks.

### Changed
- Refactored RDBI (0x22) to use the new DID registry.
- Improved `uds_config_t` to include mandatory DID table configuration.
- Unified internal function naming convention (`uds_internal_*`).

## [1.0.0] - 2026-01-30

### Added
- **White-Labelled Core**: Full refactor for commercial independence.
- **Zephyr OS Integration**: First-class support for Zephyr with native and fallback transports.
- **P2/P2* Timing Engine**: Automated NRC 0x78 (Response Pending) generation.
- **Asynchronous Service Support**: `UDS_PENDING` return status for long-running operations.
- **CMocka Unit Test Suite**: isolated tests for all core services (0x10, 0x22, 0x27, 0x3E).
- **GitHub Actions CI**: Automated static analysis, build, and test pipeline.
- **Host Simulation**: UDP-based virtual CAN for rapid PC development.
- **Commercial Documentation**: ARCHITECTURE, VISION, ROADMAP, and compliance guides.

### Changed
- Refactored `uds_core` to be dependency-injection based.
- Standardized API with `uds_ctx_t` and `uds_config_t`.

### Security
- Implemented **Security Access (SID 0x27)** state machine for seed/key exchange.
