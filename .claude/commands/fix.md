# Bug Fix Agent

Fix a reported bug or issue in the Go repository.

## Instructions

1. Understand the bug from `$ARGUMENTS`
2. Reproduce or locate the issue
3. Identify root cause (don't just fix symptoms)
4. Plan the fix
5. Implement minimally - don't refactor unrelated code
6. Add/update tests to prevent regression
7. Verify the fix

## Debugging Steps

### General
1. Find relevant code using Grep/Glob
2. Read the code to understand current behavior
3. Trace the bug to its source
4. Identify the minimal fix

### Compiler Bugs
```bash
go build -gcflags=-W <pkg>        # print IR
go build -gcflags=-S <pkg>        # print assembly
GOSSAFUNC=FuncName go build       # SSA HTML visualization
go build -gcflags=-m=2 <pkg>      # escape/inline decisions
go build -gcflags=-d=ssa/check_bce/debug  # bounds check info
```

### Runtime Bugs
```bash
GODEBUG=gctrace=1 ./program       # GC tracing
GODEBUG=schedtrace=1000 ./program # scheduler tracing
go test -race <pkg>               # race detection
```

### Test Failures
```bash
go test -v <pkg>                  # verbose output
go test -run=TestName <pkg>       # specific test
go test cmd/internal/testdir -run='Test/<file>.go'  # specific compiler test
```

## Verification

After fixing:
1. `go build ./...` - must pass
2. `go test <affected-package>` - must pass
3. `go test cmd/internal/testdir` - if compiler change
4. Manually verify the specific bug is fixed

## Output

```
## Bug Analysis
**Location**: `file.go:123`
**Root Cause**: <why it happens>

## Fix Applied
**Files Changed**:
- `file.go` - <what changed>

## Test Added
- `file_test.go` - <test description>

## Verification
- Build: PASS
- Tests: PASS
- Bug fixed: YES
```

## Common Bug Categories

| Category | Where to Look | Debug Flag |
|----------|---------------|------------|
| Miscompilation | `cmd/compile/internal/ssa` | `GOSSAFUNC` |
| Wrong escape | `cmd/compile/internal/escape` | `-gcflags=-m=2` |
| Runtime panic | `runtime/` | `GODEBUG` |
| Race condition | Various | `-race` |
| Platform-specific | `*_<os>.go`, `*_<arch>.go` | - |

## Arguments

- `$ARGUMENTS` - Bug description, error message, or issue number
