#!/bin/bash
# Run Elle consistency tests for ROJ consensus
#
# Usage:
#   ./scripts/run-elle-tests.sh              # Run full suite
#   ./scripts/run-elle-tests.sh happy        # Run single scenario
#   ./scripts/run-elle-tests.sh --download   # Download elle-cli only
#
# Environment variables:
#   ELLE_CLI_JAR - Path to elle-cli JAR (default: ~/.elle-cli/elle-cli.jar)
#   ROJ_NODES    - Number of nodes (default: scenario-dependent)
#   ROJ_OPS      - Number of operations (default: 100)

set -euo pipefail

# Configuration
ELLE_CLI_VERSION="${ELLE_CLI_VERSION:-0.1.7}"
ELLE_CLI_URL="https://github.com/ligurio/elle-cli/releases/download/${ELLE_CLI_VERSION}/elle-cli-${ELLE_CLI_VERSION}-standalone.jar"
ELLE_CLI_DIR="${HOME}/.elle-cli"
ELLE_CLI_JAR="${ELLE_CLI_JAR:-${ELLE_CLI_DIR}/elle-cli.jar}"

# Get script directory (works on Linux/macOS)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Download elle-cli if not present
download_elle_cli() {
    if [[ -f "${ELLE_CLI_JAR}" ]]; then
        log_info "elle-cli already present at ${ELLE_CLI_JAR}"
        return 0
    fi

    log_info "Downloading elle-cli ${ELLE_CLI_VERSION}..."
    mkdir -p "${ELLE_CLI_DIR}"

    if command -v curl &> /dev/null; then
        curl -L -o "${ELLE_CLI_JAR}" "${ELLE_CLI_URL}"
    elif command -v wget &> /dev/null; then
        wget -O "${ELLE_CLI_JAR}" "${ELLE_CLI_URL}"
    else
        log_error "Neither curl nor wget found. Please install one."
        exit 1
    fi

    if [[ -f "${ELLE_CLI_JAR}" ]]; then
        log_info "Downloaded elle-cli to ${ELLE_CLI_JAR}"
    else
        log_error "Failed to download elle-cli"
        exit 1
    fi
}

# Check prerequisites
check_prerequisites() {
    # Check Rust
    if ! command -v cargo &> /dev/null; then
        log_error "Cargo not found. Please install Rust: https://rustup.rs"
        exit 1
    fi

    # Check Java
    if ! command -v java &> /dev/null; then
        log_error "Java not found. Please install Java 11+: https://adoptium.net"
        exit 1
    fi

    # Verify Java version
    JAVA_VERSION=$(java -version 2>&1 | head -1 | awk -F '"' '{print $2}' | cut -d'.' -f1)
    if [[ "${JAVA_VERSION}" -lt 11 ]]; then
        log_warn "Java ${JAVA_VERSION} detected. Java 11+ recommended."
    fi

    # Download elle-cli if needed
    download_elle_cli
}

# Build the harness
build_harness() {
    log_info "Building roj-elle-harness..."
    cd "${PROJECT_DIR}"
    cargo build --release -p roj-elle-harness
}

# Run a single scenario
run_scenario() {
    local scenario="${1:-happy}"
    local nodes="${ROJ_NODES:-}"
    local ops="${ROJ_OPS:-100}"
    local output_dir="${PROJECT_DIR}/results"

    log_info "Running scenario: ${scenario}"

    mkdir -p "${output_dir}"

    local cmd="${PROJECT_DIR}/target/release/roj-elle run --scenario ${scenario} --operations ${ops} --output-dir ${output_dir}"

    if [[ -n "${nodes}" ]]; then
        cmd="${cmd} --nodes ${nodes}"
    fi

    ${cmd}

    # Check with Elle
    local history_file="${output_dir}/${scenario}-history.json"
    if [[ -f "${history_file}" ]]; then
        log_info "Checking ${scenario} with Elle..."
        ${PROJECT_DIR}/target/release/roj-elle check --history "${history_file}" --elle-jar "${ELLE_CLI_JAR}"
    fi
}

# Run full test suite
run_suite() {
    local output_dir="${PROJECT_DIR}/results"
    local junit_xml="${output_dir}/junit.xml"

    log_info "Running full test suite..."

    mkdir -p "${output_dir}"

    ${PROJECT_DIR}/target/release/roj-elle suite \
        --output-dir "${output_dir}" \
        --junit-xml "${junit_xml}" \
        --elle-jar "${ELLE_CLI_JAR}"

    log_info "Results written to ${output_dir}"
    log_info "JUnit XML: ${junit_xml}"
}

# Print usage
print_usage() {
    cat << EOF
ROJ Elle Consistency Tests

Usage:
    $0                      Run full test suite
    $0 <scenario>           Run single scenario (happy, partition, leader-crash, etc.)
    $0 --download           Download elle-cli only
    $0 --help               Show this help

Scenarios:
    happy         No faults - baseline test
    partition     Network partition mid-test
    leader-crash  Leader crashes mid-test
    message-loss  10% message loss throughout
    contention    Single key, high contention

Environment Variables:
    ELLE_CLI_JAR    Path to elle-cli JAR (default: ~/.elle-cli/elle-cli.jar)
    ROJ_NODES       Number of nodes (default: scenario-dependent)
    ROJ_OPS         Number of operations (default: 100)
EOF
}

# Main
main() {
    case "${1:-}" in
        --help|-h)
            print_usage
            exit 0
            ;;
        --download)
            download_elle_cli
            exit 0
            ;;
        "")
            check_prerequisites
            build_harness
            run_suite
            ;;
        *)
            check_prerequisites
            build_harness
            run_scenario "$1"
            ;;
    esac
}

main "$@"
