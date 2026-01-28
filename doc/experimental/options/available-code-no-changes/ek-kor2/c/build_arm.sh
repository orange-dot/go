#!/bin/bash
# Build EK-KOR v2 for STM32G474 (ARM Cortex-M4)
#
# Prerequisites:
#   - arm-none-eabi-gcc in PATH
#   - CMake 3.16+
#
# Install toolchain:
#   Ubuntu/Debian: sudo apt install gcc-arm-none-eabi
#   MSYS2: pacman -S mingw-w64-x86_64-arm-none-eabi-gcc
#   macOS: brew install arm-none-eabi-gcc

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check for ARM GCC
if ! command -v arm-none-eabi-gcc &> /dev/null; then
    echo "ERROR: arm-none-eabi-gcc not found in PATH"
    echo ""
    echo "Install with:"
    echo "  Ubuntu/Debian: sudo apt install gcc-arm-none-eabi"
    echo "  MSYS2:         pacman -S mingw-w64-x86_64-arm-none-eabi-gcc"
    echo "  macOS:         brew install arm-none-eabi-gcc"
    exit 1
fi

echo "ARM GCC found:"
arm-none-eabi-gcc --version | head -1

# Create build directory
mkdir -p build_arm
cd build_arm

# Configure with CMake
echo ""
echo "Configuring CMake..."
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake \
      -DEKK_PLATFORM=stm32g474 \
      -DCMAKE_BUILD_TYPE=Debug \
      ..

# Build
echo ""
echo "Building..."
cmake --build . -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "========================================"
echo "Build successful!"
echo "========================================"
echo ""
echo "Output files:"
echo "  build_arm/ekk_test.elf  - ELF for Renode/debugger"
echo "  build_arm/ekk_test.bin  - Binary for flashing"
echo "  build_arm/ekk_test.hex  - Intel HEX for flashing"
echo "  renode/ekk_test.elf     - Copied for Renode"
echo ""
echo "To run in Renode:"
echo "  cd ../renode"
echo "  renode ekk_single.resc"
echo "  (in monitor) start"
echo ""
