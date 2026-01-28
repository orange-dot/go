# Codebase Explorer

Explore and understand the Go repository codebase.

## Instructions

1. Parse the question/topic from `$ARGUMENTS`
2. Use Glob to find relevant files
3. Use Grep to search for patterns
4. Read key files to understand implementation
5. Trace code paths as needed
6. Summarize findings clearly

## Output Format

```
## Summary
<brief answer>

## Relevant Files
- `path/file.go:123` - <what it does>

## Code Flow
1. <step in the flow>
2. <step>

## Key Details
- <important implementation detail>
```

## Go Repository Navigation

### Compiler (`cmd/compile/`)
| Phase | Package | Key Files |
|-------|---------|-----------|
| Parsing | `internal/syntax` | `parser.go`, `scanner.go` |
| Type checking | `internal/types2` | `check.go`, `decl.go` |
| IR construction | `internal/noder` | `noder.go`, `irgen.go` |
| Inlining | `internal/inline` | `inl.go` |
| Escape analysis | `internal/escape` | `escape.go` |
| SSA generation | `internal/ssagen` | `ssa.go` |
| SSA optimization | `internal/ssa` | `*.go`, `_gen/*.rules` |
| Code generation | `cmd/internal/obj` | arch-specific files |

### Runtime (`runtime/`)
| Component | Key Files |
|-----------|-----------|
| Scheduler | `proc.go`, `runtime2.go` |
| Memory allocator | `malloc.go`, `mheap.go` |
| Garbage collector | `mgc.go`, `mgcmark.go` |
| Channels | `chan.go` |
| Maps | `map.go`, `map_fast*.go` |
| Stacks | `stack.go` |

### Standard Library (`src/`)
- Each package in its own directory
- `*_test.go` for tests
- `example_*_test.go` for examples

## Common Explorations

- "How does X work?" - trace the code path
- "Where is X defined?" - find the definition
- "What calls X?" - find usages
- "What does package X do?" - summarize package
- "How is X compiled?" - trace through compiler phases

## Arguments

- `$ARGUMENTS` - Question or topic to explore
