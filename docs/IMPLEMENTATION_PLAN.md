# Implementation Plan: Zephyr Integration and Testing

## Goal
Integrate LibUDS with Zephyr OS to allow:
1. Native Zephyr CAN/ISO-TP usage.
2. Simulated Zephyr target testing.
3. Automated validation against external UDS servers.
4. A simple test suite.

## Phase 1: Zephyr OS Integration

### 1.1. Create Zephyr Shim Layer
**Location**: `libuds/zephyr/`

- [x] Research Zephyr CAN/ISO-TP APIs.
- [ ] Create `uds_zephyr_isotp.c` (Zephyr transport wrapper).
- [ ] Create `uds_zephyr_time.c` (System clock wrapper).
- [ ] Create `CMakeLists.txt` for Zephyr builds.
- [ ] Create `Kconfig` for configuration options.
- [ ] Create `module.yml` for module definition.

### 1.2. Zephyr Example Application
**Location**: `libuds/examples/zephyr_uds_server/`

- [ ] Build ECU server example with Zephyr ISO-TP.
- [ ] Add board configuration files (native_sim, qemu_x86).
- [ ] Add overlay files for CAN.
- [ ] Document build and flash steps.

### 1.3. Zephyr Client Example
**Location**: `libuds/examples/zephyr_uds_client/`

- [ ] Build UDS client (tester) with Zephyr ISO-TP.
- [ ] Support the same board targets as the server.
- [ ] Document client-server interaction.

---

## Phase 2: External Validation

### 2.1. Integrate `driftregion/iso14229`
**Location**: `libuds/external/iso14229/`

- [ ] Add as a git submodule or vendored dependency.
- [ ] Add build steps in the test Makefile.
- [ ] Create scripts to launch the server.
- [ ] Document CAN interface setup.

### 2.2. Python Test Harness
**Location**: `libuds/tests/integration/`

- [ ] Install `py-uds`.
- [ ] Create `requirements.txt`.
- [ ] Configure server profiles.
- [ ] Create reusable test fixtures.

---

## Phase 3: Automated Testing

### 3.1. Virtual CAN Setup (Linux)
**Script**: `libuds/scripts/setup_vcan.sh`

- [ ] Automate `vcan0` creation.
- [ ] Check kernel modules (`can`, `can_raw`, `vcan`, `can_isotp`).
- [ ] Create cleanup scripts.

### 3.2. Test Runner
**Location**: `libuds/tests/run_all_tests.sh`

Test matrix:
```
┌─────────────────────────────────────────────────┐
│  LibUDS Client  │  vs  │  Test Server           │
├─────────────────┼──────┼────────────────────────┤
│  C (POSIX)      │  vs  │  libuds host_sim       │
│  C (POSIX)      │  vs  │  iso14229 server       │
│  C (POSIX)      │  vs  │  py-uds server         │
│  Python         │  vs  │  libuds host_sim       │
│  Zephyr (sim)   │  vs  │  iso14229 server       │
│  Zephyr (sim)   │  vs  │  py-uds server         │
└─────────────────┴──────┴────────────────────────┘
```

- [ ] Create runner script.
- [ ] Aggregate results.
- [ ] Write CI/CD guide.

### 3.3. Zephyr Simulation
**Tool**: Zephyr `native_sim` or QEMU

- [ ] Configure `native_sim` with virtual CAN.
- [ ] Bridge network for host-to-sim communication.
- [ ] Automate launch scripts.
- [ ] Collect and analyze logs.

---

## Phase 4: Documentation

### 4.1. Zephyr Integration Guide
**File**: `docs/ZEPHYR_INTEGRATION.md`

- [ ] Detail architecture (LibUDS in Zephyr).
- [ ] Explain build system integration.
- [ ] Compare native vs fallback transport.
- [ ] List configuration options.
- [ ] Analyze memory footprint.
- [ ] Address thread/ISR safety.

### 4.2. Testing Strategy
**File**: `docs/TESTING_STRATEGY.md`

- [ ] Define test matrix.
- [ ] List setup steps for each configuration.
- [ ] Explain CI/CD usage.
- [ ] Troubleshoot common issues.

### 4.3. Quick Start Guide
**File**: `docs/QUICKSTART_ZEPHYR.md`

- [ ] Create a 5-minute Zephyr example.
- [ ] List build and flash commands.
- [ ] Show expected output.
- [ ] Provide next steps.

---

## Dependencies

### Software
- Zephyr SDK (latest LTS)
- Python 3.8+
- Linux with SocketCAN support
- Git

### Python Packages
```
py-uds
python-can
can-isotp
pytest
```

### System Packages
```
can-utils
linux-modules-extra
```

---

## Success Criteria

✅ **Zephyr Integration**
- [ ] LibUDS builds as a module.
- [ ] Example app runs on `native_sim`.
- [ ] CAN communication works.

✅ **Independent Validation**
- [ ] Client communicates with `iso14229` server.
- [ ] Server responds to `iso14229` client.
- [ ] Standard UDS services verified.

✅ **Automated Testing**
- [ ] `run_all_tests.sh` runs the full matrix.
- [ ] All tests pass (or have expected failures).
- [ ] Total execution time < 5 minutes.

✅ **Documentation**
- [ ] All docs complete.
- [ ] Code samples build and run.
- [ ] No placeholder TODOs.

---

## Timeline Estimate

- Phase 1 (Zephyr Integration): **4-6 hours**
- Phase 2 (External Validation): **2-3 hours**
- Phase 3 (Automation): **3-4 hours**
- Phase 4 (Documentation): **2-3 hours**

**Total**: ~12-16 hours.
