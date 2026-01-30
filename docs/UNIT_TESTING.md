# Unit Testing Guide

LibUDS uses **CMocka** for its unit testing suite. Tests are located in the `tests/` directory and are integrated with the CMake build system.

## 1. Prerequisites

Ensure you have CMocka installed:
```bash
sudo apt-get install libcmocka-dev pkg-config
```

## 2. Running Tests

Tests are managed by `ctest`. To build and run all tests:

```bash
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
```

### Running Individual Suites
You can run specific test binaries directly from the `build/tests` directory:
- `./tests/test_core` - General stack logic
- `./tests/test_service_10` - Session Control
- `./tests/test_service_22` - Read Data
- `./tests/test_service_27` - Security Access
- `./tests/test_timing` - P2/P2* and S3 timing

---

## 3. Adding New Tests

1. **Create a new file**: `tests/unit/test_service_XX.c`.
2. **Use Shared Mocks**: Include `test_helpers.h` to access the global `tx_buffer` and `mock_get_time` functions.
3. **Register in CMake**: Add your test to `tests/CMakeLists.txt`:
   ```cmake
   add_uds_test(test_service_XX unit/test_service_XX.c)
   ```

### Mocking Pattern
We use `will_return()` and `expect_value()` to control the hardware/transport behavior:

```c
will_return(mock_get_time, 1000); // Set current time
expect_value(mock_tp_send, len, 3); // Expect an NRC response
will_return(mock_tp_send, 0); // Mock success
```

## 4. CI Integration
All unit tests are automatically executed by GitHub Actions on every push to the `main` or `develop` branches.
