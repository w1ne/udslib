FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and test tools
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-multilib \
    g++-multilib \
    cmake \
    ninja-build \
    device-tree-compiler \
    git \
    libcmocka-dev \
    libmbedtls-dev \
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

# Install West (for Zephyr). Ubuntu 24.04 enforces PEP 668, so allow
# system-wide installs in this throwaway CI image.
RUN pip3 install --no-cache-dir --break-system-packages west jsonschema

# Install Zephyr SDK (Minimal x86_64). Version must satisfy the SDK_VERSION
# pinned by the Zephyr revision built in CI (Zephyr main currently wants 1.0.1).
RUN wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz && \
    mkdir -p /opt/zephyr-sdk && \
    tar -xf zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz -C /opt/zephyr-sdk --strip-components=1 && \
    rm zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz && \
    /opt/zephyr-sdk/setup.sh -c

# Set environment variables
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

# Copy requirements first to leverage cache
COPY scripts/requirements.txt /tmp/requirements.txt
RUN pip3 install --break-system-packages -r /tmp/requirements.txt

# Set working directory
WORKDIR /app

# Default command runs all tests
CMD ["./scripts/run_all_tests.sh"]
