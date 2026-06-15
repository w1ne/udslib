#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# UDSLib Coverage Report Generator
# Requirements: lcov, genhtml

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
BUILD_DIR="$PROJECT_ROOT/build_coverage"

# lcov 2.x (Ubuntu 24.04) promotes several conditions to fatal errors that
# lcov 1.x merely warned about. Tolerate the benign ones so coverage still
# generates: gcov block accounting, unused exclude patterns, empty data.
LCOV_TOLERANT="--rc geninfo_unexecuted_blocks=1 --ignore-errors gcov,unused,empty,inconsistent,mismatch"
# genhtml has a different (no 'gcov') set of valid error classes.
GENHTML_TOLERANT="--ignore-errors unused,empty,inconsistent,source,category,corrupt"

echo "=== Initializing Coverage Build ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DENABLE_COVERAGE=ON -DBUILD_TESTING=ON
make -j$(nproc)

echo "=== Running Tests for Coverage ==="
echo "=== Running Tests for Coverage ==="
ctest --output-on-failure --output-junit test-results.xml

echo "=== Generating LCOV Reports ==="
lcov --capture --directory . --output-file coverage.info \
     --base-directory "$PROJECT_ROOT" $LCOV_TOLERANT

# Filter out test files and helper files from coverage
lcov --remove coverage.info "$PROJECT_ROOT/tests/*" "$PROJECT_ROOT/examples/*" \
     -o coverage_filtered.info $LCOV_TOLERANT

echo "=== Generating HTML Report ==="
genhtml coverage_filtered.info --output-directory coverage_report $GENHTML_TOLERANT

echo "=== Generating Text Summary ==="
lcov --summary coverage_filtered.info $LCOV_TOLERANT > coverage_summary.txt

echo ""
echo "✅ Coverage report generated at: $BUILD_DIR/coverage_report/index.html"
