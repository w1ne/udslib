# Testing Strategy

We employ a **three-tier validation pyramid**:

```
         ┌─────────────────────┐
         │   System Tests      │  ← Independent servers (iso14229, py-uds)
         │   (High Value)      │
         ├─────────────────────┤
         │ Integration Tests   │  ← Virtual CAN, Zephyr sim
         │ (Medium Speed)      │
         ├─────────────────────┤
         │    Unit Tests       │  ← CMocka, core logic
         │  (Fast, Frequent)   │
         └─────────────────────┘
```

## Tier 1: Unit Tests

**Goal**: Verify core logic in isolation.
**Framework**: CMocka.
**Location**: `tests/unit/`

### Coverage
- State machine transitions.
- Timer logic (S3, P2).
- Request/response parsing.
- NRC generation.

### Run Tests
```bash
mkdir -p build && cd build
cmake ..
make
ctest --output-on-failure
```

Or run the test binary directly:
```bash
./tests/unit_tests
```

## Tier 2: Integration Tests

**Goal**: Verify protocol flow with the transport layer.
**Tools**: `host_sim`, Virtual CAN, Python.
**Location**: `tests/integration/`

| Client | Server | Transport | Purpose |
|:-------|:-------|:----------|:--------|
| C (POSIX) | `host_sim` | UDP | Rapid CI/CD |
| Python | `host_sim` | UDP | Flexible scripting |
| C (POSIX) | Python (`py-uds`) | vcan0 | Client validation |
| Zephyr | `host_sim` | vcan0 | Platform integration |

### 2.1. C-to-C Test
```bash
cd udslib
bash run_integration_test.sh
```

### 2.2. Python Harness
```bash
python3 tests/integration/test_uds.py
```

### 2.3. Zephyr Native Sim
```bash
# Build Server
cd examples/zephyr_uds_server
west build -b native_sim
./build/zephyr/zephyr.exe &

# Run Client
cd ../../examples/client_demo
./uds_client_demo vcan0
```

## Tier 3: System Validation

**Goal**: Validate against independent UDS implementations.

### 3.1. Validation with `driftregion/iso14229`

**Setup**:
```bash
cd udslib/external
git clone https://github.com/driftregion/iso14229
cd iso14229
mkdir build && cd build
cmake .. && make
```

**Test (Client -> Server)**:
```bash
# Start Server
sudo ./iso14229_server vcan0 0x7E0 0x7E8

# Run Client
./uds_client_demo vcan0
```

### 3.2. Automation with `py-uds`

**Setup**:
```bash
pip install py-uds python-can can-isotp
```

**Run Test**:
```bash
python3 tests/integration/test_against_pyuds.py
```

### 3.3. Negative Testing (Fuzzing)

We script `can-isotp` to send malformed frames (invalid SIDs, truncated messages, oversized payloads) to verify robustness.

## Automation

**Script**: `udslib/scripts/run_all_tests.sh`

This logic executes:
1. Unit Tests.
2. C-to-C Integration.
3. Python Integration.
4. `iso14229` Validation.
5. `py-uds` Automation.

**Run Suite**:
```bash
cd udslib
bash scripts/run_all_tests.sh
```

## Continuous Integration

### GitHub Actions

Triggers on push/PR:
1. Installs dependencies (`can-utils`, `py-uds`).
2. Sets up `vcan0`.
3. Builds examples and tests.
4. Executes `run_all_tests.sh`.

## Troubleshooting

### "vcan0: No such device"
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up
```

### "can_isotp: Unknown symbol"
```bash
sudo modprobe can
sudo modprobe can_raw
sudo apt install linux-modules-extra-$(uname -r)
sudo modprobe can_isotp
```

### Zephyr build fails
```bash
west update
west zephyr-export
rm -rf build
west build -b native_sim --pristine
```
