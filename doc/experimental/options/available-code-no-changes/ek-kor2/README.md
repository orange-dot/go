# EK-KOR v2: Field-Centric Coordination RTOS

**Parallel C/Rust Development Project**

A novel real-time operating system for distributed coordination of modular power electronics.

## Project Structure

```
ek-kor2/
├── README.md                 # This file
├── DEVELOPMENT.md            # Development guide
│
├── spec/                     # LANGUAGE-AGNOSTIC SPECIFICATION
│   ├── api.md                # API specification
│   ├── state-machines.md     # State machine definitions
│   ├── test-vectors/         # Shared test cases (JSON)
│   │   ├── field_*.json
│   │   ├── topology_*.json
│   │   ├── consensus_*.json
│   │   └── heartbeat_*.json
│   └── state-machines/       # Visual diagrams
│
├── c/                        # C IMPLEMENTATION
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/ekk/          # Header files
│   ├── src/                  # Implementation
│   └── test/                 # Unit tests
│
├── rust/                     # RUST IMPLEMENTATION
│   ├── Cargo.toml
│   ├── src/                  # Implementation
│   └── tests/                # Integration tests
│
├── sim/                      # SIMULATION & VISUALIZATION
│   ├── multi_module.py       # Multi-module simulator
│   └── visualize.py          # Field/topology visualizer
│
└── tools/                    # BUILD & TEST TOOLS
    ├── run_tests.py          # Cross-language test runner
    └── compare_outputs.py    # Output comparison tool
```

## Key Innovations

1. **Potential Field Scheduling** - No central scheduler
2. **Topological k=7 Coordination** - Scale-free neighbor selection
3. **Threshold Consensus** - Distributed voting
4. **Adaptive Mesh Reformation** - Self-healing topology

## Development Workflow

```
┌─────────────────────────────────────────────────────────┐
│                    spec/test-vectors/                   │
│              (language-agnostic truth)                  │
└─────────────────────────────────────────────────────────┘
                         │
          ┌──────────────┴──────────────┐
          ▼                              ▼
┌─────────────────────┐      ┌─────────────────────────┐
│   C Implementation  │      │   Rust Implementation   │
│   c/src/*.c         │      │   rust/src/*.rs         │
└─────────────────────┘      └─────────────────────────┘
          │                              │
          └──────────────┬───────────────┘
                         ▼
┌─────────────────────────────────────────────────────────┐
│              tools/compare_outputs.py                   │
│         "Both implementations match spec"               │
└─────────────────────────────────────────────────────────┘
```

## Quick Start

```bash
# Run C tests
cd c && mkdir build && cd build && cmake .. && make && ctest

# Run Rust tests
cd rust && cargo test

# Run cross-language comparison
python tools/run_tests.py
```

## Module Development Order

| Week | Module | Focus |
|------|--------|-------|
| 1-2 | field | Simplest, learn basics |
| 3-4 | topology | k-neighbor algorithm |
| 5-6 | heartbeat | Timing, state machines |
| 7-8 | consensus | Most complex, voting |
| 9-10 | module | Integration |
| 11+ | hal | Hardware abstraction |

## License

MIT License - Copyright (c) 2026 Elektrokombinacija
