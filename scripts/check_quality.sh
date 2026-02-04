#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# UDSLib Stricter Code Quality Check Script

set -e

echo "=== Running Clang-Format Audit ==="
find src include examples tests -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror

echo "=== Running Cppcheck (MISRA Audit) ==="
cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --inconclusive --error-exitcode=1 -I include src/
echo "✅ Cppcheck passed."

echo "=== Running Build & Unit Test Verification ==="
BUILD_DIR="build_quality"
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"

cmake .. -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

echo "=== Executing Tests ==="
ctest --output-on-failure --output-junit report.xml -j$(nproc)

echo ""
echo "=== Test Report Summary ==="
if [ -f "report.xml" ]; then
    echo "✅ JUnit report generated at: $BUILD_DIR/report.xml"
fi

echo "=== All Quality Checks and Tests Passed! ==="
