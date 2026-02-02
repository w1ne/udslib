#!/bin/bash
# Virtual CAN Setup Script for Linux

set -e

echo "=== UDSLib Virtual CAN Setup ==="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script requires root privileges. Run with sudo."
    exit 1
fi

# Load kernel modules
echo "[1/3] Loading CAN kernel modules..."
modprobe can || echo "  (can already loaded)"
modprobe can_raw || echo "  (can_raw already loaded)"
modprobe vcan || echo "  (vcan already loaded)"
modprobe can_isotp || {
    echo "ERROR: can_isotp module not found!"
    echo "Install with: sudo apt install linux-modules-extra-\$(uname -r)"
    exit 1
}

# Create virtual CAN interface
echo "[2/3] Creating vcan0 interface..."
if ip link show vcan0 &>/dev/null; then
    echo "  vcan0 already exists, recreating..."
    ip link delete vcan0
fi

ip link add dev vcan0 type vcan
ip link set vcan0 up

# Verify setup
echo "[3/3] Verifying setup..."
if ip link show vcan0 &>/dev/null; then
    echo "✅ vcan0 is up and running"
    ip -details link show vcan0
else
    echo "❌ Failed to create vcan0"
    exit 1
fi

echo ""
echo "Virtual CAN setup complete!"
echo "You can now run: candump vcan0"
