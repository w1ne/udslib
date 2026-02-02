FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and test tools
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-multilib \
    g++-multilib \
    cmake \
    git \
    libcmocka-dev \
    lcov \
    cppcheck \
    clang-format \
    python3 \
    python3-pip \
    python3-venv \
    pkg-config \
    wget \
    curl \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Install West (for Zephyr)
RUN pip3 install --no-cache-dir west jsonschema

# Install Zephyr SDK (Minimal x86_64)
RUN wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.5/zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz && \
    mkdir -p /opt/zephyr-sdk && \
    tar -xvf zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz -C /opt/zephyr-sdk --strip-components=1 && \
    rm zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz && \
    /opt/zephyr-sdk/setup.sh -t x86_64-zephyr-elf -c

# Set environment variables
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

# Copy requirements first to leverage cache
COPY scripts/requirements.txt /tmp/requirements.txt
RUN pip3 install -r /tmp/requirements.txt

# Set working directory
WORKDIR /app

# Default command runs all tests
CMD ["./scripts/run_all_tests.sh"]
