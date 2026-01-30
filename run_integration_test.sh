#!/bin/bash
# LibUDS C-to-C Integration Test

# Get the script directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PORT=5005

echo "--- Starting C-to-C Integration Test (Port $PORT) ---"

# 1. Start Simulator in background
$DIR/examples/host_sim/uds_host_sim $PORT > $DIR/sim_c.log 2>&1 &
SIM_PID=$!
sleep 1

# 2. Run Client
$DIR/examples/client_demo/uds_client_demo 127.0.0.1 $PORT > $DIR/client_c.log 2>&1

# 3. Verify Output
grep -q "Read Data OK" $DIR/client_c.log
RESULT=$?

# 4. Cleanup
kill $SIM_PID
wait $SIM_PID 2>/dev/null

if [ $RESULT -eq 0 ]; then
    echo "SUCCESS: C Client to C Simulator communication verified!"
    echo "--- Client Log Output ---"
    cat $DIR/client_c.log
    exit 0
else
    echo "FAILED: Integration test log did not contain success markers."
    echo "--- Simulator Log ---"
    cat $DIR/sim_c.log
    echo "--- Client Log ---"
    cat $DIR/client_c.log
    exit 1
fi
