# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is the official Go programming language repository containing the compiler, runtime, and standard library.

## Build Commands

Build from source (run from `src/` directory):
- **Unix/Linux/macOS**: `./make.bash` (build) or `./all.bash` (build + test)
- **Windows**: `make.bat`

Requires Go 1.24.6+ as bootstrap compiler (set via `GOROOT_BOOTSTRAP`).

After building, use `../bin/go` or add `<repo>/bin` to PATH.

Install updated compiler after changes:
```bash
go install cmd/compile
```

## Testing

**Standard library and package tests:**
```bash
go test <package>              # test a single package
go test ./...                  # test all packages in current directory tree
go test std                    # test entire standard library
```

**Compiler and toolchain tests (black box tests in `test/` directory):**
```bash
go test cmd/internal/testdir                           # all tests in test/ dir
go test cmd/internal/testdir -run='Test/escape.*.go'   # specific files
```

**Run all tests:**
```bash
./run.bash                     # from src/, runs dist test
```

## Architecture

### Directory Structure

- `src/cmd/` - Compiler, linker, and tools (go, compile, link, asm, vet, gofmt, etc.)
- `src/runtime/` - Runtime (scheduler, GC, memory allocator) in Go + assembly
- `src/` - Standard library packages
- `test/` - Black box and regression tests for toolchain
- `api/` - API compatibility tracking files

### Compiler Phases (cmd/compile)

1. **Parsing** (`internal/syntax`) - Lexer, parser, syntax tree
2. **Type checking** (`internal/types2`) - Port of go/types
3. **IR construction** (`internal/noder`) - Convert to compiler IR
4. **Middle-end** (`internal/inline`, `internal/escape`) - Inlining, escape analysis
5. **Walk** (`internal/walk`) - Order evaluation, desugar to runtime calls
6. **SSA** (`internal/ssa`, `internal/ssagen`) - SSA generation and optimization
7. **Code generation** (`cmd/internal/obj`) - Machine code output

### Runtime (src/runtime)

Core concepts from `HACKING.md`:
- **G** (goroutine) - Lightweight user-level thread
- **M** (machine) - OS thread
- **P** (processor) - Per-CPU scheduling context; exactly `GOMAXPROCS` exist

Key directives:
- `//go:nosplit` - No stack growth check; must document why
- `//go:nowritebarrier` - Assert no write barriers
- `//go:systemstack` - Must run on system stack

Error handling: Use `throw` for unrecoverable runtime errors, `panic` for recoverable errors, `fatal` for user-fault errors (like racing map writes).

### SSA Backend (cmd/compile/internal/ssa)

Rewrite rules in `_gen/*.rules`. After modifying rules or operators:
```bash
go generate cmd/compile/internal/ssa
```

Visualize SSA for a function:
```bash
GOSSAFUNC=FunctionName go build
```
Opens `ssa.html` showing SSA at each compiler pass.

## Debugging the Compiler

```bash
go build -gcflags=-m=2                    # print optimization info (inlining, escape)
go build -gcflags=-d=ssa/check_bce/debug  # print bounds check info
go build -gcflags=-W                      # print IR after type checking
go build -gcflags=-S                      # print assembly
go tool compile -d help                   # list debug flags
go tool compile -d ssa/help               # list SSA debug flags
```

## Code Review

Uses Gerrit at https://go.googlesource.com/go. See https://go.dev/doc/contribute for contribution guidelines.

File issues via `go bug` command.

## Key Files

- `src/cmd/compile/README.md` - Compiler architecture
- `src/runtime/HACKING.md` - Runtime internals (scheduler, memory, synchronization)
- `src/cmd/compile/internal/ssa/README.md` - SSA backend details
