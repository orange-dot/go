# Code Review Agent

Review code changes for quality and correctness in the Go repository.

## Instructions

1. Check `$ARGUMENTS` for specific files or use `git diff` for staged changes
2. Review against Go project standards
3. Generate actionable feedback

## Review Criteria

### 1. Correctness
- Does the logic do what's intended?
- Are edge cases handled?
- Is concurrency handled correctly?

### 2. Go Idioms
- Follows Go conventions (Effective Go)
- Proper error handling (not swallowed, wrapped with context)
- Appropriate use of interfaces

### 3. Performance (for runtime/compiler code)
- Escape analysis: will values escape to heap unnecessarily?
- Bounds checks: can they be eliminated?
- Allocations: are there unnecessary allocations?

### 4. Runtime-Specific (if applicable)
- `//go:nosplit` - is it needed? documented why?
- Write barriers - appropriate use?
- Thread safety - proper synchronization?

### 5. Compiler-Specific (if applicable)
- SSA rules - correct and complete?
- All architectures considered?
- Tests added to `test/` directory?

### 6. Tests
- Are changes tested?
- Do tests cover edge cases?
- Compiler behavior tests in `test/`?

### 7. API Compatibility
- New public APIs documented?
- `api/go1.*.txt` updated if needed?
- Backward compatible?

## Output Format

```
## Overall Assessment
<LGTM | Needs Changes | Blocking Issues>

## Issues Found
### [Severity: High/Medium/Low] <title>
**File**: `path/file.go:123`
**Issue**: <description>
**Suggestion**: <how to fix>

## Performance Notes
- Escape analysis: <any concerns>
- Allocations: <any concerns>

## Positive Notes
- <good things about the code>

## Checklist
- [ ] Error handling complete
- [ ] Tests added/updated
- [ ] No obvious security issues
- [ ] Follows existing patterns
- [ ] API compatibility checked
- [ ] Platform considerations addressed
```

## Quick Checks

```bash
go build -gcflags=-m=2 <pkg>     # escape analysis
go build -gcflags=-S <pkg>        # assembly output
go vet ./...                      # static analysis
```

## Arguments

- `$ARGUMENTS` - Optional: specific files to review, or empty for git diff
