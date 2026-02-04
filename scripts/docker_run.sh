#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# Build and run UDSLib tests in a standardized Docker container

set -e

IMAGE_NAME="udslib-test-env"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

# Check if docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: docker is not installed or not in PATH"
    exit 1
fi

echo "=== Building Docker Image ==="
docker build -t $IMAGE_NAME -f "$PROJECT_ROOT/Dockerfile" "$PROJECT_ROOT"

if [ -n "$1" ]; then
    echo "=== Running Custom Command in Docker: $1 ==="
    docker run --rm \
        -v "$PROJECT_ROOT:/app" \
        -u $(id -u):$(id -g) \
        $IMAGE_NAME \
        /bin/bash -c "$1"
    exit $?
fi

echo "=== Running Quality Checks (Unit Tests) in Docker ==="
# Mount project root to /app
# Run as current user to avoid permission issues with artifacts
docker run --rm \
    -v "$PROJECT_ROOT:/app" \
    -u $(id -u):$(id -g) \
    $IMAGE_NAME \
    /bin/bash -c "rm -rf build_quality && ./scripts/check_quality.sh"

echo "=== Running Coverage Analysis in Docker ==="
docker run --rm \
    -v "$PROJECT_ROOT:/app" \
    -u $(id -u):$(id -g) \
    $IMAGE_NAME \
    /bin/bash -c "rm -rf build_coverage && ./scripts/generate_coverage.sh"
