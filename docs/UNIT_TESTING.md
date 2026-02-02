# Unit Testing

UDSLib uses **CMocka** for its unit testing suite. Tests are located in `tests/` and integrated with CMake.

## 1. Prerequisites

Install CMocka:
```bash
sudo apt-get install libcmocka-dev pkg-config
```

## 2. Running Tests

To build and run the full suite:

```bash
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
```

### Individual Suites
Run specific binaries from `build/tests`:
- `./tests/test_core`: General logic.
- `./tests/test_service_10`: Session Control.
- `./tests/test_service_22`: Read Data.
- `./tests/test_service_27`: Security Access.
- `./tests/test_timing`: P2/S3 timing.

## 3. Adding New Tests

1. **Create File**: `tests/unit/test_service_XX.c`.
2. **Use Mocks**: Include `test_helpers.h`.
3. **Register**: Add to `tests/CMakeLists.txt`:
   ```cmake
   add_uds_test(test_service_XX unit/test_service_XX.c)
   ```

### Mocking Pattern
Use `will_return()` and `expect_value()` to control behavior:

```c
will_return(mock_get_time, 1000);   // Set current time
expect_value(mock_tp_send, len, 3); // Expect response length
will_return(mock_tp_send, 0);       // Mock transport success
```

## 4. CI Integration
GitHub Actions executes all unit tests on every push.
