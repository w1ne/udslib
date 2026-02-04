#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# Scripts to setup minimal Zephyr environment for Simulation
# Based on Zephyr generic installation guide

set -e

# 1. System Dependencies
echo "Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    git cmake ninja-build gperf \
    ccache dfu-util device-tree-compiler wget \
    python3-dev python3-pip python3-setuptools python3-tk python3-wheel python3-venv xz-utils file \
    make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1

# 2. Setup Venv
echo "Setting up Python venv..."
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"
mkdir -p "$ZEPHYR_WORKSPACE"
python3 -m venv "$ZEPHYR_WORKSPACE/.venv"
source "$ZEPHYR_WORKSPACE/.venv/bin/activate"

# 3. Install West
echo "Installing West..."
pip install west

# 4. Initialize Workspace
echo "Initializing Zephyr Workspace..."
cd "$ZEPHYR_WORKSPACE"
if [ ! -d ".west" ]; then
    west init
    west update
else
    echo "Workspace already initialized."
    west update
fi

# 5. Export Zephyr CMake package
echo "Exporting Zephyr Package..."
west zephyr-export

# 6. Install Python Dependencies
echo "Installing Zephyr Python Requirements..."
pip install -r zephyr/scripts/requirements.txt

# 7. Setup SDK (Minimal)
# Using host compiler for native_sim, but we need minimal toolchain env?
# For native_sim on Linux, host compiler is used by default if ZEPHYR_TOOLCHAIN_VARIANT is not set or set to host.
echo "Setting toolchain variant to host..."
# We will create a setup_env.sh to source later
cat <<EOF > "$ZEPHYR_WORKSPACE/zephyr-env.sh"
source "$ZEPHYR_WORKSPACE/.venv/bin/activate"
source "$ZEPHYR_WORKSPACE/zephyr/zephyr-env.sh"
export ZEPHYR_TOOLCHAIN_VARIANT=host
EOF

echo "✅ Zephyr Setup Complete!"
echo "To use: source $ZEPHYR_WORKSPACE/zephyr-env.sh"
