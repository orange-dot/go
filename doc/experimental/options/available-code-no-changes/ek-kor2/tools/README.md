# EK-KOR2 Test Tools

Standalone test tools for the multi-layer test system.

## Tools Overview

| Tool | Purpose | Usage |
|------|---------|-------|
| `run_tests.py` | Cross-language test runner (C vs Rust) | `python run_tests.py` |
| `golden_capture.py` | Capture golden reference outputs | `python golden_capture.py --lang rust` |
| `golden_validate.py` | Validate against golden outputs | `python golden_validate.py --lang rust` |
| `reference_impl.py` | Python reference implementation | `python reference_impl.py --validate` |
| `llm_oracle.py` | LLM-assisted test generation | `python llm_oracle.py generate spec/api.md field_gradient` |
| `compare_outputs.py` | Compare two output files | `python compare_outputs.py a.json b.json` |

## Test Pyramid

```
┌──────────────────────────────────────────────────────────────┐
│                   MULTI-LAYER TEST SYSTEM                     │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  LAYER 5: MUTATION TESTING                                   │
│     cargo install cargo-mutants                              │
│     cd rust && cargo mutants                                 │
│                                                               │
│  LAYER 4: LLM ORACLE (llm_oracle.py)                         │
│     python llm_oracle.py generate spec/api.md function_name  │
│     python llm_oracle.py expected spec/api.md vector.json    │
│     python llm_oracle.py analyze spec/api.md exp.json act.json│
│                                                               │
│  LAYER 3: PYTHON REFERENCE (reference_impl.py)               │
│     python reference_impl.py test_vector.json                │
│     python reference_impl.py --validate                      │
│                                                               │
│  LAYER 2: GOLDEN OUTPUTS (golden_*.py)                       │
│     python golden_capture.py --lang rust                     │
│     python golden_validate.py --lang rust                    │
│                                                               │
│  LAYER 1: PROPERTY TESTS (spec/property-tests/)              │
│     cd spec/property-tests && cargo test                     │
│                                                               │
│  LAYER 0: CROSS-VALIDATION (run_tests.py)                    │
│     python run_tests.py                                      │
│     python run_tests.py --lang both                          │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

## Quick Start

### 1. Cross-Validation (Layer 0) - Default
```bash
# Build both implementations
cd ../c && cmake -B build && cmake --build build
cd ../rust && cargo build --bin test_harness

# Run cross-validation
python run_tests.py
```

### 2. Golden Outputs (Layer 2) - Single Implementation
```bash
# Capture golden outputs from Rust
python golden_capture.py --lang rust

# Later, validate against golden
python golden_validate.py --lang rust
```

### 3. Python Reference (Layer 3) - No Native Build
```bash
# Validate test vectors using Python reference
python reference_impl.py --validate
```

### 4. LLM Oracle (Layer 4) - AI-Assisted
```bash
# Set API key
export ANTHROPIC_API_KEY=your_key

# Generate expected output
python llm_oracle.py expected ../spec/api.md ../spec/test-vectors/field_001.json

# Generate new test vector
python llm_oracle.py generate ../spec/api.md field_gradient
```

### 5. Property Tests (Layer 1) - Mathematical Invariants
```bash
cd ../spec/property-tests
cargo test
```

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `ANTHROPIC_API_KEY` | For LLM oracle (Claude) |
| `OPENAI_API_KEY` | For LLM oracle (GPT-4 fallback) |
| `PROPTEST_CASES` | Number of property test cases (default: 256) |

## File Locations

```
ek-kor2/
├── tools/
│   ├── run_tests.py         # Cross-validation runner
│   ├── golden_capture.py    # Capture golden outputs
│   ├── golden_validate.py   # Validate vs golden
│   ├── reference_impl.py    # Python reference
│   ├── llm_oracle.py        # LLM-assisted testing
│   └── compare_outputs.py   # Output comparison
│
├── spec/
│   ├── test-vectors/        # JSON test vectors (23 files)
│   ├── golden-outputs/      # Captured golden outputs
│   ├── llm-cache/           # Cached LLM responses
│   ├── property-tests/      # Standalone proptest project
│   │   ├── Cargo.toml
│   │   ├── src/lib.rs
│   │   └── tests/
│   │       ├── prop_types.rs
│   │       ├── prop_field.rs
│   │       ├── prop_consensus.rs
│   │       ├── prop_heartbeat.rs
│   │       └── prop_topology.rs
│   └── api.md               # API specification
│
├── c/                       # C implementation
│   └── test/test_harness.c
│
└── rust/                    # Rust implementation
    └── src/bin/test_harness.rs
```
