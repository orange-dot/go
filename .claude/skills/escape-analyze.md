# Skill: escape-analyze

Analyze escape analysis decisions and optimize heap allocations.

## When to Use

- Investigating unexpected heap allocations
- Optimizing hot paths to reduce GC pressure
- Understanding why a value escapes
- Reviewing performance-critical code

## Running Escape Analysis

```bash
go build -gcflags='-m' <package>      # basic escape info
go build -gcflags='-m=2' <package>    # detailed escape info
go build -gcflags='-m -m' <package>   # even more detail
```

## Escape Reasons

| Message | Meaning | Fix |
|---------|---------|-----|
| `escapes to heap` | Value allocated on heap | See specific reason |
| `moved to heap: X` | Variable X heap-allocated | Avoid pointer escape |
| `leaking param: X` | Parameter X escapes function | Consider copying |
| `X escapes to heap: Y` | X escapes because of Y | Fix Y first |
| `too large for stack` | Value > stack limit | Split or accept heap |

## Common Escape Causes

### 1. Pointer stored in interface
```go
// Escapes:
var i interface{} = &myStruct{}  // &myStruct escapes

// Better (if type known):
var s MyStruct = myStruct{}  // stays on stack
```

### 2. Closure captures variable
```go
// Escapes:
func f() func() int {
    x := 42
    return func() int { return x }  // x escapes
}
```

### 3. Returned pointer
```go
// Escapes:
func f() *int {
    x := 42
    return &x  // x escapes
}
```

### 4. Slice/map growth
```go
// May escape:
s := make([]int, 0)
s = append(s, 1)  // may escape if grows
```

## Output Format

```
## Escape Analysis: <package/function>

## Summary
- Heap allocations: N
- Stack allocations: M
- Escaping parameters: K

## Allocation Details

### Heap Allocations (optimize these)
| Line | Variable | Reason | Suggestion |
|------|----------|--------|------------|
| 42 | x | escapes to heap | avoid interface conversion |

### Stack Allocations (good)
| Line | Variable | Notes |
|------|----------|-------|
| 15 | buf | does not escape |

## Optimization Opportunities
1. <specific suggestion>
2. <specific suggestion>

## Tradeoffs
- <any tradeoffs to consider>
```

## Compiler Source Reference

Escape analysis lives in `cmd/compile/internal/escape/`:
- `escape.go` - Main analysis logic
- `graph.go` - Data flow graph

## Example Usage

```
/escape-analyze encoding/json
/escape-analyze runtime/malloc.go
/escape-analyze -func=Marshal encoding/json
```
