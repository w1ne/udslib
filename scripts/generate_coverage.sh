#!/bin/bash
# UDSLib Coverage Report Generator
# Requirements: lcov, genhtml

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."
BUILD_DIR="$PROJECT_ROOT/build_coverage"

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
     --base-directory "$PROJECT_ROOT"

# Filter out test files and helper files from coverage
lcov --remove coverage.info "$PROJECT_ROOT/tests/*" "$PROJECT_ROOT/examples/*" -o coverage_filtered.info

echo "=== Generating HTML Report ==="
genhtml coverage_filtered.info --output-directory coverage_report

echo "=== Generating Text Summary ==="
lcov --summary coverage_filtered.info > coverage_summary.txt

echo ""
echo "✅ Coverage report generated at: $BUILD_DIR/coverage_report/index.html"
