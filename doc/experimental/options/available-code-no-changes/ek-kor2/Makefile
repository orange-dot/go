# EK-KOR v2 Master Build File
#
# Orchestrates parallel C and Rust development
#
# Usage:
#   make all       - Build both C and Rust
#   make c         - Build C only
#   make rust      - Build Rust only
#   make test      - Run all tests (C, Rust, cross-validation)
#   make sim       - Run Python simulator
#   make clean     - Clean all build artifacts

.PHONY: all c rust test test-c test-rust test-vectors sim clean format lint help

# Directories
C_DIR := c
RUST_DIR := rust
SPEC_DIR := spec
TOOLS_DIR := tools
SIM_DIR := sim
BUILD_DIR := build

# CMake configuration
CMAKE_BUILD_TYPE ?= Debug
CMAKE_PLATFORM ?= posix

# Default target
all: c rust

# ============================================================================
# C Build
# ============================================================================

c: $(BUILD_DIR)/c/Makefile
	@echo "=== Building C ==="
	$(MAKE) -C $(BUILD_DIR)/c

$(BUILD_DIR)/c/Makefile: $(C_DIR)/CMakeLists.txt
	@mkdir -p $(BUILD_DIR)/c
	cd $(BUILD_DIR)/c && cmake ../../$(C_DIR) \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
		-DEKK_PLATFORM=$(CMAKE_PLATFORM) \
		-DEKK_BUILD_TESTS=ON

c-release:
	@mkdir -p $(BUILD_DIR)/c-release
	cd $(BUILD_DIR)/c-release && cmake ../../$(C_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DEKK_PLATFORM=$(CMAKE_PLATFORM)
	$(MAKE) -C $(BUILD_DIR)/c-release

# ============================================================================
# Rust Build
# ============================================================================

rust:
	@echo "=== Building Rust ==="
	cd $(RUST_DIR) && cargo build

rust-release:
	@echo "=== Building Rust (release) ==="
	cd $(RUST_DIR) && cargo build --release

rust-embedded:
	@echo "=== Building Rust (no_std) ==="
	cd $(RUST_DIR) && cargo build --no-default-features --features embedded

# ============================================================================
# Testing
# ============================================================================

test: test-c test-rust test-vectors
	@echo "=== All tests passed ==="

test-c: c
	@echo "=== Running C tests ==="
	cd $(BUILD_DIR)/c && ctest --output-on-failure

test-rust:
	@echo "=== Running Rust tests ==="
	cd $(RUST_DIR) && cargo test

test-vectors:
	@echo "=== Running cross-language test vectors ==="
	python $(TOOLS_DIR)/run_tests.py

test-field:
	@echo "=== Running field module tests ==="
	python $(TOOLS_DIR)/run_tests.py field

test-topology:
	@echo "=== Running topology module tests ==="
	python $(TOOLS_DIR)/run_tests.py topology

test-consensus:
	@echo "=== Running consensus module tests ==="
	python $(TOOLS_DIR)/run_tests.py consensus

test-heartbeat:
	@echo "=== Running heartbeat module tests ==="
	python $(TOOLS_DIR)/run_tests.py heartbeat

# ============================================================================
# Simulation
# ============================================================================

sim:
	@echo "=== Running multi-module simulator ==="
	python $(SIM_DIR)/multi_module.py --modules 49 --ticks 1000

sim-large:
	@echo "=== Running large-scale simulation (100+ modules) ==="
	python $(SIM_DIR)/multi_module.py --modules 100 --ticks 5000

# ============================================================================
# Cross-Language Comparison
# ============================================================================

compare:
	@echo "=== Comparing C and Rust outputs ==="
	python $(TOOLS_DIR)/compare_outputs.py \
		$(BUILD_DIR)/c/output.json \
		$(BUILD_DIR)/rust/output.json

# ============================================================================
# Code Quality
# ============================================================================

format: format-c format-rust

format-c:
	@echo "=== Formatting C code ==="
	find $(C_DIR) -name "*.c" -o -name "*.h" | xargs clang-format -i

format-rust:
	@echo "=== Formatting Rust code ==="
	cd $(RUST_DIR) && cargo fmt

lint: lint-c lint-rust

lint-c: c
	@echo "=== Linting C code ==="
	@# Static analysis with clang-tidy if available
	-find $(C_DIR)/src -name "*.c" | xargs clang-tidy --quiet \
		-p $(BUILD_DIR)/c 2>/dev/null || true

lint-rust:
	@echo "=== Linting Rust code ==="
	cd $(RUST_DIR) && cargo clippy -- -D warnings

# ============================================================================
# Documentation
# ============================================================================

docs: docs-c docs-rust

docs-c:
	@echo "=== Generating C documentation ==="
	doxygen Doxyfile 2>/dev/null || echo "Doxygen not installed"

docs-rust:
	@echo "=== Generating Rust documentation ==="
	cd $(RUST_DIR) && cargo doc --no-deps

# ============================================================================
# Clean
# ============================================================================

clean:
	@echo "=== Cleaning build artifacts ==="
	rm -rf $(BUILD_DIR)
	cd $(RUST_DIR) && cargo clean

clean-c:
	rm -rf $(BUILD_DIR)/c $(BUILD_DIR)/c-release

clean-rust:
	cd $(RUST_DIR) && cargo clean

# ============================================================================
# Development Utilities
# ============================================================================

# Watch for changes and rebuild
watch-c:
	@echo "Watching C files for changes..."
	while true; do \
		inotifywait -r -e modify $(C_DIR)/src $(C_DIR)/include; \
		$(MAKE) c; \
	done

watch-rust:
	cd $(RUST_DIR) && cargo watch -x build

# Generate test harness skeleton
gen-test-harness:
	@echo "Generating test harness from test vectors..."
	python $(TOOLS_DIR)/gen_test_harness.py

# Check spec consistency
check-spec:
	@echo "Checking specification consistency..."
	python $(TOOLS_DIR)/check_spec.py

# ============================================================================
# Help
# ============================================================================

help:
	@echo "EK-KOR v2 Build System"
	@echo "======================"
	@echo ""
	@echo "Build targets:"
	@echo "  all            Build both C and Rust"
	@echo "  c              Build C library"
	@echo "  rust           Build Rust library"
	@echo "  c-release      Build C in release mode"
	@echo "  rust-release   Build Rust in release mode"
	@echo "  rust-embedded  Build Rust in no_std mode"
	@echo ""
	@echo "Test targets:"
	@echo "  test           Run all tests"
	@echo "  test-c         Run C unit tests"
	@echo "  test-rust      Run Rust unit tests"
	@echo "  test-vectors   Run cross-language test vectors"
	@echo "  test-field     Test field module only"
	@echo "  test-topology  Test topology module only"
	@echo "  test-consensus Test consensus module only"
	@echo "  test-heartbeat Test heartbeat module only"
	@echo ""
	@echo "Simulation:"
	@echo "  sim            Run basic simulation (49 modules)"
	@echo "  sim-large      Run large simulation (100+ modules)"
	@echo ""
	@echo "Code quality:"
	@echo "  format         Format all code"
	@echo "  lint           Run linters"
	@echo "  docs           Generate documentation"
	@echo ""
	@echo "Utilities:"
	@echo "  clean          Clean all build artifacts"
	@echo "  watch-c        Watch C files and rebuild"
	@echo "  watch-rust     Watch Rust files and rebuild"
	@echo ""
	@echo "Configuration:"
	@echo "  CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)"
	@echo "  CMAKE_PLATFORM=$(CMAKE_PLATFORM)"
