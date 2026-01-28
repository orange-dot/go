# Implementation Agent

Implement a planned feature or fix in the Go repository.

## Instructions

1. Read the plan if one was created (check conversation history)
2. If no plan exists, first run `/plan $ARGUMENTS`
3. Implement changes following the plan
4. After each file change, verify builds
5. Use existing code patterns - don't reinvent
6. Handle errors properly with context

## Verification Loop

After implementation:

### 1. Build Verification
```bash
go build ./...                    # general build
go install cmd/compile            # if compiler changes
go generate cmd/compile/internal/ssa  # if SSA rules changed
```

### 2. Test Verification
```bash
go test <modified-package>        # unit tests
go test cmd/internal/testdir      # compiler tests (if applicable)
go vet ./...                      # static analysis
```

### 3. Quality Checks
```bash
go build -gcflags=-m=2 <pkg>      # check escape analysis impact
gofmt -d <files>                  # formatting check
```

## On Failure

- Fix the issue
- Re-run verification
- Only report success when all checks pass

## Go-Specific Implementation Notes

### Compiler Changes
- Follow existing patterns in the same phase
- Update tests in `test/` directory if adding new behavior
- Consider all GOOS/GOARCH combinations

### Runtime Changes
- Be extremely careful with concurrency
- Document `//go:nosplit`, `//go:nowritebarrier` usage
- Test with race detector: `go test -race`

### Standard Library
- Follow existing package conventions
- Update documentation strings
- Add examples if appropriate
- Check api/go1.*.txt for API additions

## Arguments

- `$ARGUMENTS` - Feature description or "from plan"
