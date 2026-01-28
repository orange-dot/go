# Skill: ssa-debug

Debug and visualize SSA (Static Single Assignment) for Go functions.

## When to Use

- Investigating compiler optimization issues
- Understanding why code compiles a certain way
- Debugging miscompilation bugs
- Learning how Go optimizations work
- Verifying SSA rewrite rules are applied

## How SSA Visualization Works

Set `GOSSAFUNC=FunctionName` to generate `ssa.html`:

```bash
# Windows (PowerShell)
$env:GOSSAFUNC="FunctionName"; go build

# Windows (cmd)
set GOSSAFUNC=FunctionName && go build

# Unix/WSL
GOSSAFUNC=FunctionName go build
```

This creates `ssa.html` showing SSA at each compiler pass.

## SSA Passes to Examine

| Pass | Purpose |
|------|---------|
| `start` | Initial SSA from IR |
| `opt` | General optimizations |
| `prove` | Bounds check elimination |
| `lower` | Architecture-specific lowering |
| `regalloc` | Register allocation |
| `genssa` | Final code generation |

## Debug Flags

```bash
go build -gcflags='-d=ssa/help'           # list all SSA debug options
go build -gcflags='-d=ssa/prove/debug=2'  # prove pass debugging
go build -gcflags='-d=ssa/opt/debug=1'    # optimization debugging
go build -gcflags='-d=ssa/check_bce/debug=1'  # bounds check info
```

## Output Format

```
## SSA Analysis: <FunctionName>

## Key Observations
- Pass X: <what happens>
- Optimization applied: <description>

## Passes of Interest
| Pass | Change | Impact |
|------|--------|--------|
| prove | BCE eliminated at line N | Performance improvement |

## Values to Track
- v123: <what this value represents>

## Issues Found
- <any unexpected behavior>

## Recommendations
- <suggestions for improvement>
```

## Common Patterns

### Bounds Check Elimination
Look at `prove` pass - if BCE fails, check:
- Is slice length known?
- Is index provably < length?

### Escape to Heap
If value unexpectedly escapes:
- Check `start` pass for heap allocations
- Use `-gcflags=-m=2` for escape analysis details

### Missing Optimization
- Compare `start` vs `opt` passes
- Check if expected rewrite rule fired
- Look for patterns in `_gen/*.rules`

## Example Usage

```
/ssa-debug encoding/json.Unmarshal
/ssa-debug runtime.makeslice
/ssa-debug mypackage.MyFunction
```
