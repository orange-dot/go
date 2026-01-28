# EK-KOR v2 Build Environment
#
# Usage:
#   docker build -t ekk-build .
#   docker run --rm -v ${PWD}:/workspace ekk-build
#
# For interactive shell:
#   docker run -it --rm -v ${PWD}:/workspace ekk-build bash

FROM ubuntu:22.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    python3 \
    python3-pip \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    && rm -rf /var/lib/apt/lists/*

# Install Renode (optional, for emulation)
RUN apt-get update && apt-get install -y wget gnupg \
    && wget https://github.com/renode/renode/releases/download/v1.14.0/renode_1.14.0_amd64.deb \
    && apt-get install -y ./renode_1.14.0_amd64.deb \
    && rm renode_1.14.0_amd64.deb \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Default command: build and test
CMD ["bash", "-c", "cd c && mkdir -p build && cd build && cmake .. -G Ninja && ninja && ctest --output-on-failure"]
