# Skill: codegen-rules

Work with SSA rewrite rules for code generation optimization.

## When to Use

- Adding new compiler optimizations
- Fixing miscompilation from bad rules
- Understanding how patterns are optimized
- Adding architecture-specific optimizations

## Rule File Locations

Rules are in `cmd/compile/internal/ssa/_gen/`:

| File | Purpose |
|------|---------|
| `generic.rules` | Architecture-independent optimizations |
| `AMD64.rules` | x86-64 specific rules |
| `ARM64.rules` | ARM64 specific rules |
| `386.rules` | x86 32-bit rules |
| `ARM.rules` | ARM 32-bit rules |
| `*Ops.go` | Operation definitions |

## Rule Syntax

```
// Pattern => Replacement
(Add64 x (Const64 [c])) && c == 0 => x

// Multiple results
(Load ptr mem) && t.IsBoolean() => (IsNonNil ptr)

// Auxiliary matching
(MOVQload [off] {sym} ptr mem) => ...
```

### Components
- `(Op args...)` - Match/create operation
- `[auxint]` - Match auxiliary integer
- `{aux}` - Match auxiliary value
- `&& condition` - Boolean guard
- `=>` - Rewrite to
- `@block` - Specify block

## Regenerating Rules

After modifying `.rules` files:

```bash
cd src/cmd/compile/internal/ssa
go generate
```

This produces `rewrite*.go` files.

## Testing Rules

```bash
# Run all SSA tests
go test cmd/compile/internal/ssa

# Test specific architecture
GOARCH=amd64 go test cmd/compile/internal/ssa

# Verify rule fires
GOSSAFUNC=MyFunc go build -gcflags='-d=ssa/opt/debug=2'
```

## Output Format

```
## Rule Analysis

## Existing Rules for <pattern>
| File | Line | Rule | Purpose |
|------|------|------|---------|
| generic.rules | 123 | (Add64 x (Const64 [0])) => x | Identity optimization |

## Proposed Rule
```
<the new rule>
```

## Verification
- [ ] Syntax correct
- [ ] Condition complete (no false positives)
- [ ] All architectures considered
- [ ] Test case added

## Test Case
```go
// test/codegen/mytest.go
func testCase() {
    // amd64:"EXPECTED_ASM"
    ...
}
```

## Potential Conflicts
- <any rules that might interact>
```

## Common Patterns

### Constant Folding
```
(Add64 (Const64 [c]) (Const64 [d])) => (Const64 [c+d])
```

### Strength Reduction
```
(Mul64 x (Const64 [c])) && isPowerOfTwo(c) => (Lsh64x64 x (Const64 [log2(c)]))
```

### Dead Code Elimination
```
(If (ConstBool [true]) yes no) => (First yes no)
```

### Architecture-Specific
```
// AMD64.rules
(ADDQ x (MOVQconst [c])) && is32Bit(c) => (ADDQconst [c] x)
```

## Writing Good Rules

1. **Be specific** - Narrow conditions prevent miscompilation
2. **Order matters** - Earlier rules take precedence
3. **Test edge cases** - Negative numbers, overflow, zero
4. **Consider all arches** - Generic rules apply everywhere
5. **Add comments** - Explain non-obvious optimizations

## Compiler Source Reference

- `cmd/compile/internal/ssa/` - SSA package
- `cmd/compile/internal/ssa/_gen/` - Rule definitions
- `cmd/compile/internal/ssa/rewrite*.go` - Generated rewriters

## Example Usage

```
/codegen-rules search "Mul64"
/codegen-rules explain (Lsh64x64 x (Const64 [3]))
/codegen-rules add "strength reduction for multiply by 3"
```
