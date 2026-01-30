# Testing Strategy & Automation

LibUDS is designed for high reliability, supported by a multi-tiered testing strategy.

## 1. Unit Tests
- **Location**: `tests/`
- **Framework**: Minimalist C assertion framework (for quick checks) and **CMocka** (for robust, fast, and industry-standard validation).
- **Scope**: Validates core state machine, initialization, and individual service logic in isolation.
- **CMocka Benefits**: Provides powerful mocking capabilities (essential for HAL abstraction) and memory leak detection, ensuring the stack remains "pure play" and dependency-free.
- **Run**: `cd tests && make && ./uds_tests`

## 2. ECU Simulator (Host Sim)
- **Location**: `examples/host_sim/`
- **Purpose**: A standalone POSIX application that acts as a real ECU.
- **Virtual CAN**: It implements a **Virtual CAN over UDP** interface. It listens for UDP packets (acting as CAN frames) and responds accordingly.
- **Robust Alternatives**: For production-grade validation, we recommend using [**uds_sim**](git@github.com:example-org/uds_sim.git), a high-fidelity UDS simulator.
- **Usage**:
  ```bash
  ./uds_host_sim [PORT]
  ```

## 3. UDS Simulation Options: Comparative Analysis

For a "robust" testing environment, we offer three tiers of simulation:

| Option | Pros | Cons | Best For |
| :--- | :--- | :--- | :--- |
| **libuds `host_sim`** (Internal) | Zero dependencies; uses exact production C code; extremely fast. | Circular verification (testing libuds against libuds). | Rapid CI/CD; stack logic verification. |
| **`uds_sim`** (External) | **Recommended.** High-fidelity; golden standard; independent codebase. | Requires external setup/cloning. | Acceptance testing; Production-ready simulation. |
| **Python `scapy` / `udsoncan`** | Highly flexible; great for negative testing (corruption/fuzzing). | Slower; requires Python environment. | Security testing; Robustness under error conditions. |

## 4. Why this is "Robust"
...
Unlike simple unit tests, the integration suite:
1.  **Tests Real Timing**: Validates that timers like S3 actually work in a live loop.
2.  **Validates ISO-TP**: Reuses the same `uds_tp_isotp.c` used in production, ensuring segmentation and reassembly are correct.
3.  **Separates Concerns**: Tests the Client stack against a real Server stack execution.
