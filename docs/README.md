# UDSLib Documentation Index

This library provides a portable, commercial-grade UDS (ISO 14229) protocol stack for automotive diagnostics.

## 📖 Core Documentation

### Getting Started
- **[QUICKSTART_ZEPHYR.md](QUICKSTART_ZEPHYR.md)**: 5-minute Zephyr example.
- **[Docker](../scripts/docker_run.sh)**: Run tests instantly with Docker.
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

### Service Coverage & Compliance
- **[SERVICE_COMPLIANCE.md](SERVICE_COMPLIANCE.md)**: Authoritative ISO 14229-1 service matrix (27/27).
- **[MISRA.md](MISRA.md)**: MISRA-C:2012 baseline and documented deviations.

### Testing & Validation
- **[TESTING.md](TESTING.md)**: Testing philosophy.
- **[TESTING_STRATEGY.md](TESTING_STRATEGY.md)**: Three-tier testing approach (Unit, Integration, System).
- **[UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md)**: Independent UDS stacks for cross-validation (`iso14229`, `py-uds`, `udsoncan`).

## 🎯 Quick Navigation

- **Understand the architecture**: [ARCHITECTURE.md](ARCHITECTURE.md)
- **Integrate with Zephyr**: [ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)
- **Use as a client**: [CLIENT_API.md](CLIENT_API.md)
- **Port to OS (RTOS/Bare Metal)**: [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)
- **Set up testing**: [TESTING_STRATEGY.md](TESTING_STRATEGY.md)
- **Understand ISO-TP**: [TRANSPORT.md](TRANSPORT.md)

## 📂 Repository Structure

```
udslib/
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
| ISO 14229-1 application services (27/27) | Implemented (see [SERVICE_COMPLIANCE.md](SERVICE_COMPLIANCE.md)) |
| Asynchronous processing (NRC 0x78) | Implemented |
| OS Abstraction Layer (two-context concurrency) | Implemented (see [OSAL.md](OSAL.md)) |
| Memory services (0x23 / 0x3D) | Implemented |
| Flash engine (0x31 / 0x34 / 0x36 / 0x37) | Implemented |
| Authentication (0x29) | Implemented |
| DTC management (0x14 / 0x19 / 0x85) | Implemented |
| Zephyr integration | Implemented |
| ISO-TP fallback (Classic CAN + CAN-FD) | Implemented |
| MISRA-C:2012 (CI-checked baseline) | No mandatory-rule violations |

## 📝 License

- Community: PolyForm Noncommercial 1.0.0 (noncommercial use only). See `../LICENSE`.
- Commercial: 5,000 EUR, includes integration help + 1 year support. See `../COMMERCIAL_LICENSE.md` or email `andrii@shylenko.com`.
- Evaluation: development/testing allowed under the community terms; no production or for-profit deployment without the commercial license.
