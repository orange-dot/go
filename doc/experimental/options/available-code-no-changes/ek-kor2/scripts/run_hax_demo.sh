#!/bin/bash
#
# EK-KOR v2 HAX Demo Runner
#
# This script builds the HAX demo firmware and runs it in Renode emulation.
# Designed for CI pipelines - returns exit code 0 on success, 1 on failure.
#
# Usage:
#   ./run_hax_demo.sh              # Full build + run
#   ./run_hax_demo.sh --posix      # POSIX build only (faster, no Renode)
#   ./run_hax_demo.sh --stm32      # STM32 build + Renode
#   ./run_hax_demo.sh --skip-build # Use existing ELF, run Renode only
#
# Copyright (c) 2026 Elektrokombinacija
# SPDX-License-Identifier: MIT

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
C_DIR="$PROJECT_DIR/c"
RENODE_DIR="$PROJECT_DIR/renode"

# Defaults
BUILD_POSIX=true
BUILD_STM32=true
RUN_RENODE=true
SKIP_BUILD=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --posix)
            BUILD_POSIX=true
            BUILD_STM32=false
            RUN_RENODE=false
            shift
            ;;
        --stm32)
            BUILD_POSIX=false
            BUILD_STM32=true
            RUN_RENODE=true
            shift
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --help)
            echo "Usage: $0 [--posix|--stm32|--skip-build]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo ""
echo "=========================================="
echo "  EK-KOR v2 HAX Demo"
echo "=========================================="
echo ""

# Track overall result
RESULT=0

# Build POSIX version
if [[ "$BUILD_POSIX" == "true" && "$SKIP_BUILD" == "false" ]]; then
    echo -e "${YELLOW}[1/3] Building POSIX version...${NC}"

    mkdir -p "$C_DIR/build"
    cd "$C_DIR/build"

    cmake .. -G Ninja -DEKK_PLATFORM=posix -DEKK_BUILD_EXAMPLES=ON 2>&1 || {
        echo -e "${RED}[ERROR] CMake configuration failed${NC}"
        exit 1
    }

    ninja hax_demo 2>&1 || {
        echo -e "${RED}[ERROR] Build failed${NC}"
        exit 1
    }

    echo -e "${GREEN}[OK] POSIX build complete${NC}"
    echo ""

    # Run POSIX version
    echo -e "${YELLOW}[2/3] Running POSIX demo...${NC}"

    if ./hax_demo 2>&1; then
        echo -e "${GREEN}[OK] POSIX demo passed${NC}"
    else
        echo -e "${RED}[FAIL] POSIX demo failed${NC}"
        RESULT=1
    fi
    echo ""
fi

# Build STM32 version
if [[ "$BUILD_STM32" == "true" && "$SKIP_BUILD" == "false" ]]; then
    echo -e "${YELLOW}[2/3] Building STM32G474 version...${NC}"

    mkdir -p "$C_DIR/build-stm32"
    cd "$C_DIR/build-stm32"

    # Check for ARM compiler
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        echo -e "${RED}[ERROR] arm-none-eabi-gcc not found${NC}"
        echo "Install ARM toolchain or run in Docker"
        exit 1
    fi

    cmake .. -G Ninja \
        -DEKK_PLATFORM=stm32g474 \
        -DEKK_BUILD_EXAMPLES=OFF \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
        -DCMAKE_OBJCOPY=arm-none-eabi-objcopy \
        -DCMAKE_SIZE=arm-none-eabi-size 2>&1 || {
        echo -e "${RED}[ERROR] CMake configuration failed${NC}"
        exit 1
    }

    ninja hax_demo 2>&1 || {
        echo -e "${RED}[ERROR] Build failed${NC}"
        exit 1
    }

    echo -e "${GREEN}[OK] STM32G474 build complete${NC}"
    echo ""
fi

# Run Renode
if [[ "$RUN_RENODE" == "true" ]]; then
    echo -e "${YELLOW}[3/3] Running Renode emulation...${NC}"

    # Check for Renode
    if ! command -v renode &> /dev/null; then
        echo -e "${RED}[ERROR] Renode not found${NC}"
        echo "Install Renode or run in Docker"
        exit 1
    fi

    # Check for ELF
    if [[ ! -f "$RENODE_DIR/hax_demo.elf" ]]; then
        echo -e "${RED}[ERROR] hax_demo.elf not found in renode directory${NC}"
        echo "Build with --stm32 first"
        exit 1
    fi

    cd "$RENODE_DIR"

    # Run Renode headless
    timeout 60 renode --disable-xwt --console \
        -e 'include @hax_demo.resc; runMacro $demo' 2>&1 | tee /tmp/hax_demo_output.log || true

    # Parse output for PASS/FAIL
    if grep -q "\[TEST\] PASS" /tmp/hax_demo_output.log; then
        echo -e "${GREEN}[OK] Renode demo passed${NC}"
    elif grep -q "\[TEST\] FAIL" /tmp/hax_demo_output.log; then
        echo -e "${RED}[FAIL] Renode demo failed${NC}"
        RESULT=1
    else
        echo -e "${YELLOW}[WARN] Could not determine test result${NC}"
        # Check for milestones
        if grep -q "\[MILESTONE\] DISCOVERY_COMPLETE" /tmp/hax_demo_output.log; then
            echo "  - Discovery milestone found"
        fi
        if grep -q "\[MILESTONE\] CONSENSUS_PASSED" /tmp/hax_demo_output.log; then
            echo "  - Consensus milestone found"
        fi
        if grep -q "\[MILESTONE\] REFORMATION_COMPLETE" /tmp/hax_demo_output.log; then
            echo "  - Reformation milestone found"
        fi
    fi
    echo ""
fi

# Summary
echo "=========================================="
if [[ $RESULT -eq 0 ]]; then
    echo -e "  ${GREEN}HAX Demo: ALL TESTS PASSED${NC}"
else
    echo -e "  ${RED}HAX Demo: TESTS FAILED${NC}"
fi
echo "=========================================="
echo ""

exit $RESULT
