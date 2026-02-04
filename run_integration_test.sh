#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# UDSLib C-to-C Integration Test

# Get the script directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PORT=5005
ARTIFACT_DIR="$DIR/artifacts/integration"
mkdir -p "$ARTIFACT_DIR"

echo "--- Starting C-to-C Integration Test (Port $PORT) ---"

# Resolve binaries (support out-of-tree builds)
SIM_CANDIDATES=(
    "$DIR/examples/host_sim/uds_host_sim"
    "$DIR/build/examples/host_sim/uds_host_sim"
    "$DIR/build_quality/examples/host_sim/uds_host_sim"
)
CLIENT_CANDIDATES=(
    "$DIR/examples/client_demo/uds_client_demo"
    "$DIR/build/examples/client_demo/uds_client_demo"
)

SIM_BIN=""
for c in "${SIM_CANDIDATES[@]}"; do
    if [ -x "$c" ]; then
        SIM_BIN="$c"
        break
    fi
done

CLIENT_BIN=""
for c in "${CLIENT_CANDIDATES[@]}"; do
    if [ -x "$c" ]; then
        CLIENT_BIN="$c"
        break
    fi
done

if [ -z "$SIM_BIN" ]; then
    echo "Simulator binary not found. Build with: cmake -S . -B build && cmake --build build --target uds_host_sim"
    exit 1
fi

if [ -z "$CLIENT_BIN" ]; then
    echo "Client demo binary not found. Build with: make -C examples/client_demo"
    exit 1
fi

# 1. Start Simulator in background
$SIM_BIN $PORT > "$ARTIFACT_DIR/sim_c.log" 2>&1 &
SIM_PID=$!
sleep 1

# 2. Run Client
$CLIENT_BIN 127.0.0.1 $PORT > "$ARTIFACT_DIR/client_c.log" 2>&1

# 3. Verify Output
grep -q "Read Data OK" "$ARTIFACT_DIR/client_c.log"
RESULT=$?

# 4. Cleanup
kill $SIM_PID
wait $SIM_PID 2>/dev/null

if [ $RESULT -eq 0 ]; then
    echo "SUCCESS: C Client to C Simulator communication verified!"
    echo "--- Client Log Output ---"
    cat "$ARTIFACT_DIR/client_c.log"
    exit 0
else
    echo "FAILED: Integration test log did not contain success markers."
    echo "--- Simulator Log ---"
    cat "$ARTIFACT_DIR/sim_c.log"
    echo "--- Client Log ---"
    cat "$ARTIFACT_DIR/client_c.log"
    exit 1
fi
