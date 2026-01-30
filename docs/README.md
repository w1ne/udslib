# LibUDS Documentation Index

This library provides a portable, commercial-grade UDS (ISO 14229) protocol stack for automotive diagnostics.

## 📖 Core Documentation

### Getting Started
- **[VISION.md](VISION.md)**: Project vision and commercial model.
- **[QUICKSTART_ZEPHYR.md](QUICKSTART_ZEPHYR.md)**: 5-minute Zephyr example.
- **[ROADMAP.md](ROADMAP.md)**: Development timelines.

### Architecture & Design
- **[ARCHITECTURE.md](ARCHITECTURE.md)**: Design philosophy and diagrams.
- **[TRANSPORT.md](TRANSPORT.md)**: Transport layer architecture.
- **[TIMING_AND_TIMEOUTS.md](TIMING_AND_TIMEOUTS.md)**: P2/P2* and S3 logic.
- **[OSAL.md](OSAL.md)**: Thread safety and RTOS integration.
- **[CLIENT_API.md](CLIENT_API.md)**: Client (tester) API usage.
- **[UNIT_TESTING.md](UNIT_TESTING.md)**: Testing guide.

### Platform Integration
- **[ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)**: Complete Zephyr OS integration guide.
  - Native ISO-TP vs Fallback.
  - Build system (Kconfig, CMake).
  - Memory analysis.
  - RTOS considerations.

### Testing & Validation
- **[TESTING.md](TESTING.md)**: Testing philosophy.
- **[TESTING_STRATEGY.md](TESTING_STRATEGY.md)**: Three-tier testing approach (Unit, Integration, System).
- **[UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md)**: External UDS simulator comparison (`iso14229`, `py-uds`).

### Implementation
- **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)**: Current development plan.

## 🎯 Quick Navigation

- **Understand the architecture**: [ARCHITECTURE.md](ARCHITECTURE.md)
- **Integrate with Zephyr**: [ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)
- **Use as a client**: [CLIENT_API.md](CLIENT_API.md)
- **Set up testing**: [TESTING_STRATEGY.md](TESTING_STRATEGY.md)
- **Compare UDS servers**: [UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md)
- **Understand ISO-TP**: [TRANSPORT.md](TRANSPORT.md)

## 📂 Repository Structure

```
libuds/
├── docs/                    ← Documentation
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
│   └── zephyr_uds_server/  ← Zephyr example
├── tests/
│   ├── unit/               ← CMocka unit tests
│   └── integration/        ← Python & C integration tests
├── external/
│   └── iso14229/           ← External validation
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

Commercial license available. See [VISION.md](VISION.md).
