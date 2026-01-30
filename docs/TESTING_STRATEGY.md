# Comprehensive Testing Strategy

This document outlines the complete testing strategy for LibUDS, from unit tests to full system validation using multiple independent implementations.

## Testing Philosophy

**"Test the implementation, not just the code"**

We employ a **three-tier validation pyramid**:

```
         ┌─────────────────────┐
         │   System Tests      │  ← Independent servers (iso14229, py-uds)
         │   (Slow, High Value)│
         ├─────────────────────┤
         │ Integration Tests   │  ← Virtual CAN, Zephyr sim
         │ (Medium Speed)      │
         ├─────────────────────┤
         │    Unit Tests       │  ← CMocka, core logic
         │  (Fast, Frequent)   │
         └─────────────────────┘
```

## Tier 1: Unit Tests

**Goal**: Verify core logic in isolation  
**Framework**: CMocka (robust, industry-standard)  
**Location**: `tests/unit/`  
**Execution Time**: < 5 seconds

### Coverage

- State machine transitions (session, security levels)
- Timer logic (S3, P2, P2*)
- Request/response parsing
- NRC generation
- Memory boundary conditions

### Running Unit Tests

```bash
cd tests/unit
make
./test_uds_core
```

### Example Test

```c
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "uds_core.h"

static void test_session_transition(void **state) {
    uds_ctx_t ctx;
    // ... init ...
    
    // Request Extended Session
    uint8_t req[] = {0x10, 0x03};
    uds_input_sdu(&ctx, req, 2);
    
    assert_int_equal(ctx.session, UDS_SESSION_EXTENDED);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_transition),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

## Tier 2: Integration Tests

**Goal**: Verify protocol flow with transport layer  
**Tools**: LibUDS host_sim, Virtual CAN, Python scripts  
**Location**: `tests/integration/`  
**Execution Time**: < 1 minute

### Test Matrix

| Client | Server | Transport | Purpose |
|:-------|:-------|:----------|:--------|
| C (POSIX) | `host_sim` | UDP | Rapid CI/CD |
| Python | `host_sim` | UDP | Flexible scripting |
| C (POSIX) | Python (`py-uds`) | vcan0 | Client validation |
| Zephyr (native_sim) | `host_sim` | vcan0 | Platform integration |

### 2.1. C-to-C Test (Current)

**Script**: `libuds/run_integration_test.sh`

```bash
cd libuds
bash run_integration_test.sh
```

**What it tests**:
- DiagnosticSessionControl (0x10)
- ReadDataByIdentifier (0x22)
- Multi-frame ISO-TP

### 2.2. Python Test Harness

**Script**: `tests/integration/test_uds.py`

```bash
python3 tests/integration/test_uds.py
```

**What it tests**:
- Session transitions
- Security Access flow (Seed/Key)
- S3 timeout behavior
- Negative Response Codes

### 2.3. Zephyr Native Sim Test

**Prerequisites**:
```bash
# Setup virtual CAN
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up
```

**Build & Run**:
```bash
cd examples/zephyr_uds_server
west build -b native_sim
./build/zephyr/zephyr.exe &

# Test with C client
cd ../../examples/client_demo
./uds_client_demo vcan0
```

## Tier 3: System Validation (External Servers)

**Goal**: Validate against independent UDS implementations  
**Critical**: Ensures ISO 14229 compliance, not just self-validation

### 3.1. Validation with `driftregion/iso14229`

**Why**: Independent C codebase, both server AND client implementations

#### Setup

```bash
cd libuds/external
git clone https://github.com/driftregion/iso14229
cd iso14229
mkdir build && cd build
cmake ..
make
```

#### Test 1: LibUDS Client → iso14229 Server

```bash
# Terminal 1: Start iso14229 server
cd libuds/external/iso14229/build
sudo ./iso14229_server vcan0 0x7E0 0x7E8

# Terminal 2: Run LibUDS client
cd libuds/examples/client_demo
./uds_client_demo vcan0

# Expected: Successful session change and data read
```

#### Test 2: iso14229 Client → LibUDS Server

```bash
# Terminal 1: Start LibUDS server
cd libuds/examples/host_sim
./uds_host_sim_vcan vcan0

# Terminal 2: Run iso14229 client
cd ../external/iso14229/build
./iso14229_client vcan0 0x7E0 0x7E8

# Expected: Server responds correctly to all standard services
```

#### Validation Checklist

- [ ] DiagnosticSessionControl (0x10)
- [ ] ECUReset (0x11)
- [ ] SecurityAccess (0x27)
- [ ] CommunicationControl (0x28)
- [ ] TesterPresent (0x3E)
- [ ] ReadDataByIdentifier (0x22)
- [ ] WriteDataByIdentifier (0x2E)
- [ ] RoutineControl (0x31)
- [ ] RequestDownload (0x34)
- [ ] TransferData (0x36)
- [ ] RequestTransferExit (0x37)

### 3.2. Automation with `py-uds`

**Why**: Flexible scripting, server simulation, negative testing

#### Setup

```bash
pip install py-uds python-can can-isotp
```

#### Create `py-uds` Server Profile

**File**: `tests/integration/pyuds_server_config.json`

```json
{
  "server": {
    "interface": "socketcan",
    "channel": "vcan0",
    "rx_id": "0x7E0",
    "tx_id": "0x7E8",
    "services": {
      "DiagnosticSessionControl": {
        "sessions": ["default", "programming", "extended"]
      },
      "SecurityAccess": {
        "levels": [1],
        "seed": "DEADBEEF",
        "key_algo": "xor"
      },
      "ReadDataByIdentifier": {
        "0xF190": "LIBUDS_TEST_VIN"
      }
    }
  }
}
```

#### Test Script

```python
# tests/integration/test_against_pyuds.py
from uds import UDSServer
import subprocess
import time

# Start py-uds server
server = UDSServer.from_config("pyuds_server_config.json")
server.start()

# Run LibUDS client
proc = subprocess.Popen(["./examples/client_demo/uds_client_demo", "vcan0"])
proc.wait()

assert proc.returncode == 0, "Client test failed"
```

### 3.3. Negative Testing & Fuzzing

**Goal**: Verify robustness under error conditions

```python
# tests/integration/negative_tests.py
import can
from can_isotp import Address, CanStack

# Send malformed frames
bus = can.interface.Bus(channel='vcan0', bustype='socketcan')

# Test 1: Invalid SID
addr = Address(rxid=0x7E0, txid=0x7E8)
stack = CanStack(bus, address=addr)
stack.send(b'\xFF\x00')  # Invalid SID

# Test 2: Truncated message
stack.send(b'\x10')  # Missing subfunction

# Test 3: Oversized payload
stack.send(b'\x22' + b'\xAA' * 5000)  # Exceeds ISO-TP limit

# Verify: LibUDS should send NRC or ignore, not crash
```

## Automated Test Orchestration

**Script**: `libuds/scripts/run_all_tests.sh`

```bash
#!/bin/bash
set -e

echo "=== LibUDS Comprehensive Test Suite ==="

# Phase 1: Unit Tests
echo "[1/5] Running unit tests..."
cd tests/unit && make clean && make && ./test_uds_core
echo "✅ Unit tests passed"

# Phase 2: C-to-C Integration
echo "[2/5] Running C-to-C integration..."
cd ../..
bash run_integration_test.sh
echo "✅ C-to-C passed"

# Phase 3: Python Integration
echo "[3/5] Running Python integration..."
python3 tests/integration/test_uds.py
echo "✅ Python tests passed"

# Phase 4: External Validation (iso14229)
echo "[4/5] Running iso14229 validation..."
bash tests/integration/test_iso14229.sh
echo "✅ iso14229 validation passed"

# Phase 5: py-uds Automation
echo "[5/5] Running py-uds automation..."
python3 tests/integration/test_against_pyuds.py
echo "✅ py-uds tests passed"

echo ""
echo "🎉 ALL TESTS PASSED 🎉"
```

**Run all tests**:
```bash
cd libuds
bash scripts/run_all_tests.sh
```

## Continuous Integration

### GitHub Actions Example

```yaml
# .github/workflows/test.yml
name: LibUDS Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y can-utils linux-modules-extra-$(uname -r)
          pip3 install cmocka py-uds python-can can-isotp pytest
          sudo modprobe vcan
      
      - name: Setup Virtual CAN
        run: |
          sudo ip link add dev vcan0 type vcan
          sudo ip link set vcan0 up
      
      - name: Build LibUDS
        run: |
          make -C examples/host_sim
          make -C examples/client_demo
          make -C tests/unit
      
      - name: Run Tests
        run: bash scripts/run_all_tests.sh
```

## Performance Benchmarks

| Test | Execution Time | Frequency |
|:-----|:---------------|:----------|
| Unit tests | < 5s | Every commit |
| C-to-C integ | < 10s | Every commit |
| Python integ | < 15s | Every commit |
| iso14229 val | < 30s | Pre-release |
| Full matrix | < 2 min | Nightly |
| Zephyr sim | < 3 min | Nightly |

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
# Ensure west is initialized
west update
west zephyr-export

# Clean build
rm -rf build
west build -b native_sim --pristine
```

## Next Steps

1. **Review** [ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md) for platform-specific details
2. **Explore** [UDS_SERVER_OPTIONS.md](UDS_SERVER_OPTIONS.md) for external validation tools
3. **Try** [QUICKSTART_ZEPHYR.md](QUICKSTART_ZEPHYR.md) for a hands-on example
