# Skill: gerrit-prep

Prepare changes for submission to Go's Gerrit code review system.

## When to Use

- Before submitting changes to the Go project
- Creating properly formatted commits
- Understanding the contribution workflow
- Preparing change lists (CLs)

## Prerequisites

1. **Sign CLA**: https://cla.developers.google.com/clas
2. **Install git-codereview**:
   ```bash
   go install golang.org/x/review/git-codereview@latest
   ```
3. **Configure Git**:
   ```bash
   git config --global alias.change '!git codereview change'
   git config --global alias.mail '!git codereview mail'
   git config --global alias.sync '!git codereview sync'
   ```

## Workflow

### 1. Create Branch
```bash
git checkout -b my-feature master
```

### 2. Make Changes
- Edit files
- Run tests: `go test ./...`
- Run vet: `go vet ./...`
- Format: `gofmt -w .`

### 3. Create Change
```bash
git add <files>
git change  # or: git codereview change

# This opens editor for commit message
```

### 4. Commit Message Format
```
<package>: <short description>

<longer description if needed>

Fixes #12345
```

Example:
```
encoding/json: fix unmarshal of null into non-pointer

When unmarshaling JSON null into a non-pointer value,
return an error instead of silently doing nothing.

Fixes #56789
```

### 5. Send for Review
```bash
git mail  # or: git codereview mail
```

### 6. Update After Feedback
```bash
# Make changes
git add <files>
git change  # amends the existing change
git mail    # sends updated change
```

## Commit Message Rules

1. **First line**: `<package>: <imperative summary>` (≤72 chars)
2. **Blank line** after first line
3. **Body**: Explain what and why (not how)
4. **References**: `Fixes #N`, `Updates #N`, `For #N`

### Good Examples
```
runtime: reduce lock contention in memory allocator
cmd/compile: eliminate redundant nil checks
net/http: add support for HTTP/3
```

### Bad Examples
```
Fixed the bug           # no package, not descriptive
json: changes           # too vague
encoding/json: This commit adds a new feature  # not imperative
```

## Pre-Submit Checklist

```
## Pre-Submit Checklist

### Code Quality
- [ ] `go build ./...` passes
- [ ] `go test <affected packages>` passes
- [ ] `go vet ./...` passes
- [ ] `gofmt -w .` applied

### For Compiler Changes
- [ ] `go test cmd/internal/testdir` passes
- [ ] `go generate cmd/compile/internal/ssa` run (if rules changed)
- [ ] All architectures considered

### For Runtime Changes
- [ ] `go test -race <package>` passes
- [ ] GODEBUG options documented (if new)

### For Standard Library
- [ ] `api/next.txt` updated (if new public API)
- [ ] Documentation updated
- [ ] Examples added (if appropriate)

### Commit Message
- [ ] Package prefix correct
- [ ] Summary is imperative mood
- [ ] Body explains why (not just what)
- [ ] Issue linked with `Fixes #N` or `Updates #N`

### Tests
- [ ] New tests added for new behavior
- [ ] Regression test for bug fixes
- [ ] Tests in `test/` directory (for compiler behavior)
```

## Output Format

```
## Gerrit Preparation

## Change Summary
- Package: <package>
- Type: <bug fix / feature / optimization / cleanup>
- Issue: #<number> (if applicable)

## Commit Message
```
<suggested commit message>
```

## Files Changed
| File | Changes |
|------|---------|
| path/file.go | <description> |

## Pre-Submit Status
- Build: PASS/FAIL
- Tests: PASS/FAIL
- Vet: PASS/FAIL
- Format: OK/needs gofmt

## Remaining Items
- [ ] <any remaining work>
```

## Gerrit Web Interface

- Go Gerrit: https://go-review.googlesource.com/
- View your changes: https://go-review.googlesource.com/q/owner:me
- Documentation: https://go.dev/doc/contribute

## Common Issues

| Issue | Solution |
|-------|----------|
| "no files to commit" | Stage files with `git add` first |
| Commit hook fails | Install hooks: `git codereview hooks` |
| Need to update CL | Use `git change` (amends), not new commit |
| Merge conflicts | `git codereview sync`, resolve, `git change` |

## Example Usage

```
/gerrit-prep check
/gerrit-prep message "fix nil pointer in json decoder"
/gerrit-prep workflow
```
