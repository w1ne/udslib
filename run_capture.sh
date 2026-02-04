#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# run_capture.sh
# Runs the simulation while capturing traffic to a PCAP and generating an HTML report.
# Usage: ./run_capture.sh [classic]

MODE="fd"
if [ "$1" == "classic" ]; then
    MODE="classic"
    ENABLE_FD=0
else
    ENABLE_FD=1
fi

ARTIFACT_DIR="artifacts/capture"
mkdir -p "$ARTIFACT_DIR"

PCAP_FILE="${ARTIFACT_DIR}/sim_traffic_${MODE}.pcap"
HTML_FILE="${ARTIFACT_DIR}/session_report_${MODE}.html"
SIM_LOG="${ARTIFACT_DIR}/sim_c_${MODE}.log"
CLIENT_LOG="${ARTIFACT_DIR}/client_c_${MODE}.log"

echo "=== Starting Proxy Capture ($MODE) ==="
# Start Proxy: Listen 5005 ---> Forward 5006
python3 tools/udp_proxy_pcap.py 5005 127.0.0.1 5006 "$PCAP_FILE" > /dev/null 2>&1 &
PID_PROXY=$!
sleep 1

echo "=== Starting Simulation (Port 5006, FD=$ENABLE_FD) ==="
# Start Simulator manually on 5006 (Proxy target)
mkdir -p build/examples/host_sim
if [ -x examples/host_sim/uds_host_sim ]; then
    SIM=examples/host_sim/uds_host_sim
else
    SIM=build/examples/host_sim/uds_host_sim
fi

$SIM 5006 $ENABLE_FD > "$SIM_LOG" 2>&1 &
PID_SIM=$!
sleep 1

echo "=== Running Client (Target 5005, FD=$ENABLE_FD) ==="
# Client connects to 5005 (Proxy)
if [ -x examples/client_demo/uds_client_demo ]; then
    CLIENT=examples/client_demo/uds_client_demo
else
    CLIENT=build/examples/client_demo/uds_client_demo
fi

$CLIENT 127.0.0.1 5005 $ENABLE_FD > "$CLIENT_LOG" 2>&1

echo "=== Cleanup ==="
kill $PID_SIM $PID_PROXY
wait $PID_SIM $PID_PROXY 2>/dev/null

echo "=== Analyzing PCAP ==="
python3 tools/read_pcap.py "$PCAP_FILE"

echo "=== Generating HTML Report ==="
python3 tools/pcap_to_html.py "$PCAP_FILE" "$HTML_FILE"
echo "✅ Report ready: file://$(pwd)/$HTML_FILE"
