# Testing Strategy & Automation

LibUDS uses a multi-tiered testing strategy.

## 1. Unit Tests
- **Location**: `tests/unit/`
- **Framework**: Minimalist C assertions and **CMocka**.
- **Scope**: Validates core state machine, initialization, and service logic in isolation.
- **Benefits**: Mocking capabilities allow testing without hardware dependencies.
- **Run**: `cd build && ctest --output-on-failure` or `./tests/unit_tests`

## 2. ECU Simulator (Host Sim)
- **Location**: `examples/host_sim/`
- **Purpose**: A standalone POSIX application acting as an ECU.
- **Interface**: Implements **Virtual CAN over UDP**. It listens for UDP packets (simulating CAN frames) and responds.
- **Usage**:
  ```bash
  ./uds_host_sim [PORT]
  ```

## 3. Simulation Options

We support three tiers of simulation:

| Option | Pros | Cons | Best For |
| :--- | :--- | :--- | :--- |
| **libuds `host_sim`** (Internal) | Zero dependencies. Uses production C code. Fast. | Circular verification (LibUDS testing itself). | Rapid CI/CD. |
| **`uds_sim`** (External) | Independent codebase. High-fidelity. | Requires external setup. | Acceptance testing. |
| **Python `scapy` / `udsoncan`** | Flexible. Good for negative testing. | Slower. Requires Python. | Security and robustness testing. |

## 4. Integration Benefits

Unlike simple unit tests, the integration suite:
1.  **Tests Real Timing**: Validates timers (like S3) in a live loop.
2.  **Validates ISO-TP**: Reuses the production `uds_tp_isotp.c` to ensure correct segmentation/reassembly.
3.  **Separates Concerns**: Tests the Client stack against a real Server stack execution.

## 5. CI/CD & Quality Gates

To ensure code stability in the Gitflow workflow, **GitHub Actions** are configured to run tests on every push.

### Workflow Triggers
- **Pushes**: `develop`, `main`
- **Pull Requests**: To `develop`, `main`

### Enforcing Quality Gates (Branch Protection)

You must configure GitHub to **block merges** unless tests pass.

1.  Go to **Settings** > **Branches**.
2.  Click **Add branch protection rule**.
3.  **Branch name pattern**: `develop` (Repeat for `main`).
4.  Check **Require status checks to pass before merging**.
5.  Search for and select the following Status Checks:
    - `build-posix` (Build & Unit Tests)
    - `static-analysis` (Cppcheck & Clang-Format)
    - `coverage` (Code Coverage generation)
6.  Click **Create**.

This ensures no broken code can be merged into `develop` or released to `main`.
