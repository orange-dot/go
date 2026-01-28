# Implementation Planner

Plan implementation before writing code in the Go repository.

## Instructions

1. Understand the requested feature or fix from `$ARGUMENTS`
2. Explore relevant code paths using Glob and Grep
3. Identify which subsystem is involved:
   - `cmd/compile/*` - Compiler phases
   - `runtime/*` - Runtime (scheduler, GC, memory)
   - `src/<pkg>` - Standard library
   - `cmd/*` - Tools (go, link, asm, vet)
4. Check for existing patterns in the codebase to follow
5. Consider Go compatibility requirements (api/ files)
6. Write a clear plan

## Output Format

```
## Summary
<one sentence description>

## Subsystem
<which part of Go: compiler, runtime, stdlib, tools>

## Files to Modify
- `path/to/file.go` - <what changes>

## Implementation Steps
1. <step>
2. <step>

## Testing Plan
- Unit tests: `go test <package>`
- Compiler tests: `go test cmd/internal/testdir -run='Test/<pattern>'`
- Manual verification: <steps>

## Compatibility Considerations
- API changes: <does api/*.txt need updating?>
- Backward compat: <any breaking changes?>
- Platform-specific: <any OS/arch concerns?>

## Risks
- <any concerns>
```

## Go-Specific Considerations

### Compiler Changes
- Which phase? (parsing, type-checking, IR, SSA, codegen)
- Need to run `go generate`?
- Need `go install cmd/compile`?

### Runtime Changes
- Thread-safety considerations
- GC interaction
- Need `//go:nosplit` or other directives?

### Standard Library
- API compatibility (check api/go1.*.txt)
- Documentation updates needed?
- Example code updates?

## Arguments

- `$ARGUMENTS` - Feature or fix description
