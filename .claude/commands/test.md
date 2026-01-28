# Test Runner

Run tests for the Go repository (compiler, runtime, standard library).

## Instructions

1. Determine what to test based on `$ARGUMENTS`:
   - Package name: `go test <package>`
   - `std`: `go test std` (entire standard library)
   - `compiler` or `toolchain`: `go test cmd/internal/testdir`
   - `all`: Run `./run.bash` (Unix/WSL) or equivalent
   - Specific test file pattern: `go test cmd/internal/testdir -run='Test/<pattern>'`

2. For Windows users, prefer `go test` commands over shell scripts
3. Report failures with file:line references
4. For SSA rule changes, ensure `go generate cmd/compile/internal/ssa` was run

## Test Categories

| Category | Command | Use When |
|----------|---------|----------|
| Single package | `go test encoding/json` | Testing specific package |
| Package tree | `go test ./src/encoding/...` | Testing package family |
| Standard library | `go test std` | Full stdlib verification |
| Compiler tests | `go test cmd/internal/testdir` | Testing compiler behavior |
| Specific compiler test | `go test cmd/internal/testdir -run='Test/escape.*.go'` | Single test file |
| All tests | `./run.bash` (from src/) | Pre-submit verification |

## On Failure

- Identify the failing test and root cause
- Check if it's a flaky test or real regression
- For compiler test failures, check if SSA rules need regeneration
- Suggest a fix but don't implement unless asked

## Verification Flags

Useful flags for deeper verification:
```
go test -race <package>        # race detector
go test -cover <package>       # coverage
go test -bench=. <package>     # run benchmarks
go test -v <package>           # verbose output
```

## Arguments

- `$ARGUMENTS` - Optional: package name, "std", "compiler", test pattern, or empty for context-based selection
