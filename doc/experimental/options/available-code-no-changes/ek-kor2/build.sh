#!/bin/bash
# EK-KOR v2 Build Script for Unix/Linux/macOS
#
# Usage:
#   ./build.sh           # Build all
#   ./build.sh c         # Build C only
#   ./build.sh rust      # Build Rust only
#   ./build.sh test      # Run all tests
#   ./build.sh sim       # Run simulator
#   ./build.sh clean     # Clean all

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
C_DIR="$PROJECT_ROOT/c"
RUST_DIR="$PROJECT_ROOT/rust"
TOOLS_DIR="$PROJECT_ROOT/tools"
SIM_DIR="$PROJECT_ROOT/sim"

# Configuration
BUILD_TYPE="${BUILD_TYPE:-Debug}"
PLATFORM="${PLATFORM:-posix}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

header() {
    echo -e "\n${CYAN}=== $1 ===${NC}"
}

success() {
    echo -e "${GREEN}$1${NC}"
}

warn() {
    echo -e "${YELLOW}$1${NC}"
}

error() {
    echo -e "${RED}$1${NC}"
}

build_c() {
    header "Building C"

    C_BUILD_DIR="$BUILD_DIR/c"
    mkdir -p "$C_BUILD_DIR"

    pushd "$C_BUILD_DIR" > /dev/null
    cmake "$C_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DEKK_PLATFORM="$PLATFORM" \
        -DEKK_BUILD_TESTS=ON

    cmake --build . --config "$BUILD_TYPE"
    popd > /dev/null

    success "C build successful"
}

build_rust() {
    header "Building Rust"

    pushd "$RUST_DIR" > /dev/null
    if [ "$BUILD_TYPE" = "Release" ]; then
        cargo build --release
    else
        cargo build
    fi
    popd > /dev/null

    success "Rust build successful"
}

test_c() {
    header "Running C tests"

    C_BUILD_DIR="$BUILD_DIR/c"
    if [ ! -d "$C_BUILD_DIR" ]; then
        warn "C not built yet. Building..."
        build_c
    fi

    pushd "$C_BUILD_DIR" > /dev/null
    ctest --output-on-failure --build-config "$BUILD_TYPE"
    popd > /dev/null

    success "C tests passed"
}

test_rust() {
    header "Running Rust tests"

    pushd "$RUST_DIR" > /dev/null
    cargo test
    popd > /dev/null

    success "Rust tests passed"
}

test_vectors() {
    header "Running cross-language test vectors"

    python3 "$TOOLS_DIR/run_tests.py"

    success "Test vectors passed"
}

run_sim() {
    header "Running multi-module simulator"

    python3 "$SIM_DIR/multi_module.py" --modules 49 --ticks 1000
}

clean_all() {
    header "Cleaning build artifacts"

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        warn "Removed $BUILD_DIR"
    fi

    pushd "$RUST_DIR" > /dev/null
    cargo clean
    warn "Cleaned Rust artifacts"
    popd > /dev/null

    success "Clean complete"
}

show_help() {
    cat << EOF

EK-KOR v2 Build Script (Shell)
==============================

Usage:
    ./build.sh [target]

Targets:
    all           Build both C and Rust (default)
    c             Build C library
    rust          Build Rust library
    test          Run all tests
    test-c        Run C unit tests
    test-rust     Run Rust unit tests
    test-vectors  Run cross-language test vectors
    sim           Run Python simulator
    clean         Clean all build artifacts
    help          Show this help message

Environment Variables:
    BUILD_TYPE    Debug or Release (default: Debug)
    PLATFORM      posix, stm32g474, or tricore (default: posix)

Examples:
    ./build.sh                           # Build all in Debug mode
    BUILD_TYPE=Release ./build.sh c      # Build C in Release mode
    ./build.sh test                      # Run all tests
    ./build.sh sim                       # Run simulator

EOF
}

# Main
TARGET="${1:-all}"

case "$TARGET" in
    all)
        build_c
        build_rust
        ;;
    c)
        build_c
        ;;
    rust)
        build_rust
        ;;
    test)
        test_c
        test_rust
        test_vectors
        ;;
    test-c)
        test_c
        ;;
    test-rust)
        test_rust
        ;;
    test-vectors)
        test_vectors
        ;;
    sim)
        run_sim
        ;;
    clean)
        clean_all
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        error "Unknown target: $TARGET"
        show_help
        exit 1
        ;;
esac
