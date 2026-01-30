#!/bin/bash
# LibUDS Code Quality Check Script

set -e

echo "=== Running Clang-Format Check ==="
find src include examples -name "*.c" -o -name "*.h" | xargs clang-format -i
echo "✅ Code formatted."

echo "=== Running Cppcheck Static Analysis ==="
cppcheck --enable=all --suppress=missingIncludeSystem --error-exitcode=1 -I include src/
echo "✅ Cppcheck passed."

echo "=== All Quality Checks Passed! ==="
