# LibUDS Documentation Index

Welcome to the LibUDS documentation. This library provides a portable, commercial-grade UDS (ISO 14229) protocol stack for automotive diagnostics.

## 📖 Core Documentation

### Getting Started
- **[VISION.md](VISION.md)** - Project vision and commercial model
- **[QUICKSTART_ZEPHYR.md](QUICKSTART_ZEPHYR.md)** - 5-minute Zephyr example
- **[ROADMAP.md](ROADMAP.md)** - Development roadmap and timelines

### Architecture & Design
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Core design philosophy and component diagrams
- **[TRANSPORT.md](TRANSPORT.md)** - "Spliced" transport layer architecture
- **[TIMING_AND_TIMEOUTS.md](TIMING_AND_TIMEOUTS.md)** - P2/P2* and S3 logic
- **[OSAL.md](OSAL.md)** - Thread-safety and RTOS integration guidelines
- **[CLIENT_API.md](CLIENT_API.md)** - Using LibUDS as a UDS client (tester)
- **[UNIT_TESTING.md](UNIT_TESTING.md)** - Guide for running and writing tests

### Platform Integration
- **[ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)** - Complete Zephyr OS integration guide
  - Native ISO-TP vs Fallback comparison
  - Build system (Kconfig, CMake, module.yml)
  - Memory footprint (~9-11KB)
  - Thread safety and RTOS considerations

### Testing & Validation
- **[TESTING.md](TESTING.md)** - Overview of testing philosophy
- **[TESTING_STRATEGY.md](TESTING_STRATEGY.md)** - Comprehensive three-tier testing approach
  - Unit tests (CMocka)
  - Integration tests (C, Python, Zephyr sim)
  - System validation (external servers)
- **[UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md)** - Comparison of external UDS simulators
  - `driftregion/iso14229` (recommended golden standard)
  - `py-uds` (Python automation)

### Implementation
- **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** - Current development plan
### Implementation
- **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** - Current development plan
  - Phase 1-9: completed
  - Phase 10: Memory Services & Enterprise Hardening (Done)
  - Phase 11: Portability Audit (Done)

## 🎯 Quick Navigation

**I want to...**
- ✅ **Understand the architecture** → [ARCHITECTURE.md](ARCHITECTURE.md)
- ✅ **Integrate with Zephyr** → [ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)
- ✅ **Use as a client** → [CLIENT_API.md](CLIENT_API.md)
- ✅ **Set up testing** → [TESTING_STRATEGY.md](TESTING_STRATEGY.md)
- ✅ **Compare UDS servers** → [UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md)
- ✅ **Understand ISO-TP** → [TRANSPORT.md](TRANSPORT.md)

## 📂 Repository Structure

```
libuds/
├── docs/                    ← You are here
├── src/
│   ├── core/               ← UDS protocol logic
│   └── transport/          ← ISO-TP fallback implementation
├── include/uds/            ← Public API headers
├── zephyr/                 ← Zephyr module integration
│   ├── module.yml
│   ├── Kconfig
│   └── CMakeLists.txt
├── examples/
│   ├── host_sim/           ← POSIX ECU simulator
│   ├── client_demo/        ← POSIX UDS client
│   └── zephyr_uds_server/  ← Zephyr example (coming soon)
├── tests/
│   ├── unit/               ← CMocka unit tests
│   └── integration/        ← Python & C integration tests
├── external/
│   └── iso14229/           ← External validation (cloned)
└── scripts/
    ├── setup_vcan.sh       ← Virtual CAN setup
    └── run_all_tests.sh    ← Test orchestration
```

## 🚀 Status

| Component | Status |
|:----------|:-------|
| Core UDS Stack (15 Services) | ✅ Complete (v1.3.0) |
| OS Abstraction Layer (OSAL) | ✅ Complete |
| Memory Services (0x23/0x3D) | ✅ Complete |
| Flash Engine (0x31/34/36/37) | ✅ Complete |
| Authentication (0x29) | ✅ Complete |
| DTC Management (0x14/19/85) | ✅ Complete |
| Zephyr Integration | ✅ Complete |
| ISO-TP Fallback | ✅ Complete |
| Unit Tests (100% Coverage) | ✅ Complete |
| Portability (Endian/Headers) | ✅ Verified |

## 📝 License

Commercial license available. See [VISION.md](VISION.md) for details.

---

**Questions?** Check [TESTING_STRATEGY.md](TESTING_STRATEGY.md) for troubleshooting.
