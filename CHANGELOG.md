# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
