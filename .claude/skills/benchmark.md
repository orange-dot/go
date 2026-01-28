# Skill: benchmark

Run and analyze Go benchmarks for performance testing.

## When to Use

- Measuring performance impact of changes
- Comparing before/after implementations
- Identifying performance regressions
- Optimizing hot paths

## Running Benchmarks

```bash
# Run all benchmarks in package
go test -bench=. <package>

# Run specific benchmark
go test -bench=BenchmarkName <package>

# Run with memory stats
go test -bench=. -benchmem <package>

# Multiple iterations for stability
go test -bench=. -count=10 <package>

# Set benchmark time
go test -bench=. -benchtime=5s <package>

# CPU profiling
go test -bench=. -cpuprofile=cpu.out <package>

# Memory profiling
go test -bench=. -memprofile=mem.out <package>
```

## Comparing Results

Use `benchstat` for statistical comparison:

```bash
# Install benchstat
go install golang.org/x/perf/cmd/benchstat@latest

# Run before
go test -bench=. -count=10 <package> > old.txt

# Make changes, then run after
go test -bench=. -count=10 <package> > new.txt

# Compare
benchstat old.txt new.txt
```

## Benchmark Output Format

```
BenchmarkName-8    1000000    1234 ns/op    56 B/op    2 allocs/op
```

| Field | Meaning |
|-------|---------|
| `-8` | GOMAXPROCS |
| `1000000` | Iterations run |
| `1234 ns/op` | Nanoseconds per operation |
| `56 B/op` | Bytes allocated per operation |
| `2 allocs/op` | Allocations per operation |

## Benchstat Output

```
name        old time/op  new time/op  delta
Marshal-8   1.23µs ± 2%  1.15µs ± 1%  -6.50% (p=0.001 n=10+10)
```

| Field | Meaning |
|-------|---------|
| `± 2%` | Variation across runs |
| `-6.50%` | Performance change |
| `p=0.001` | Statistical significance |
| `n=10+10` | Samples in each set |

## Output Format

```
## Benchmark Analysis: <package>

## Environment
- Go version: <version>
- GOOS/GOARCH: <os/arch>
- CPU: <if relevant>

## Results

### Before
| Benchmark | ns/op | B/op | allocs/op |
|-----------|-------|------|-----------|
| Name | 1234 | 56 | 2 |

### After
| Benchmark | ns/op | B/op | allocs/op |
|-----------|-------|------|-----------|
| Name | 1100 | 48 | 1 |

### Comparison (benchstat)
| Benchmark | Delta | Significant |
|-----------|-------|-------------|
| Name | -10.9% | Yes (p<0.05) |

## Analysis
- <what improved>
- <what regressed>
- <tradeoffs>

## Recommendations
1. <suggestion>
2. <suggestion>
```

## Writing Good Benchmarks

```go
func BenchmarkX(b *testing.B) {
    // Setup outside the loop
    data := setupData()

    b.ResetTimer() // Don't count setup

    for i := 0; i < b.N; i++ {
        // Code to benchmark
        result = process(data)
    }

    // Prevent compiler optimization
    _ = result
}
```

## Common Issues

| Issue | Solution |
|-------|----------|
| High variance | Increase `-count`, close other apps |
| Compiler optimizes away | Use result, call `runtime.KeepAlive` |
| Setup included | Call `b.ResetTimer()` after setup |
| GC during benchmark | Call `runtime.GC()` before, or accept it |

## Profiling Deep Dive

```bash
# Generate profile
go test -bench=BenchmarkX -cpuprofile=cpu.out <package>

# Analyze
go tool pprof cpu.out

# Web UI
go tool pprof -http=:8080 cpu.out
```

## Example Usage

```
/benchmark encoding/json
/benchmark compare runtime/map
/benchmark -bench=Marshal encoding/json
```
