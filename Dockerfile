FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and test tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libcmocka-dev \
    lcov \
    cppcheck \
    clang-format \
    python3 \
    python3-pip \
    pkg-config \
    wget \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first to leverage cache
COPY scripts/requirements.txt /tmp/requirements.txt
RUN pip3 install -r /tmp/requirements.txt

# Set working directory
WORKDIR /app

# Default command runs all tests
CMD ["./scripts/run_all_tests.sh"]
