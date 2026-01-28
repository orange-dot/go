# MAPF-HET Research

Multi-Agent Path Finding with Heterogeneous Agents - algorithm research for coordinated robot battery swap operations.

## Structure

```
mapf-het-research/
├── cmd/mapfhet/     # CLI entry point
├── internal/
│   ├── core/        # Domain models (workspace, robots, tasks)
│   ├── algo/        # Algorithm implementations
│   └── sim/         # Discrete-event simulation
└── testdata/        # Benchmark scenarios
```

## Quick Start

```bash
go test ./...
go run ./cmd/mapfhet
```

## Algorithms

| Algorithm | Status | File |
|-----------|--------|------|
| CBS-HET | stub | `algo/cbs.go` |
| Prioritized | stub | `algo/prioritized.go` |
| ECBS-HET | planned | - |

## References

See `docs/` for problem formulation and integration details.
