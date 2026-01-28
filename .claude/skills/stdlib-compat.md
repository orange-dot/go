# Skill: stdlib-compat

Check and maintain Go standard library API compatibility.

## When to Use

- Adding new public APIs to standard library
- Modifying existing public APIs
- Before submitting stdlib changes
- Reviewing API additions for Go releases

## API Compatibility Files

Location: `api/`

| File | Contents |
|------|----------|
| `go1.txt` | Go 1.0 API |
| `go1.1.txt` | APIs added in Go 1.1 |
| `go1.N.txt` | APIs added in Go 1.N |
| `next.txt` | APIs for next release (development) |
| `except.txt` | Exceptions to compatibility |

## API File Format

```
pkg <package>, type <Name> <underlying>
pkg <package>, func <Name>(<params>) <returns>
pkg <package>, method (<receiver>) <Name>(<params>) <returns>
pkg <package>, const <Name> <type>
pkg <package>, var <Name> <type>
```

## Checking Compatibility

```bash
# Regenerate API files and check for differences
go tool api -c api/go1.txt,api/go1.1.txt,...,api/go1.N.txt -next api/next.txt
```

## Adding New APIs

1. Implement the feature
2. Add entry to `api/next.txt` (sorted alphabetically within package)
3. Run api tool to verify

Example `api/next.txt` entry:
```
pkg encoding/json, func NewDecoder2(io.Reader) *Decoder
pkg encoding/json, method (*Decoder) DisallowUnknownFields2()
```

## Output Format

````markdown
## API Compatibility Check

## Package: <package>

## New APIs Added
| Type | Signature | Notes |
|------|-----------|-------|
| func | NewFunc() error | Added for X purpose |

## API File Updates Required
```
pkg <package>, func NewFunc() error
```

## Breaking Changes Detected
- <any breaking changes - these block submission>

## Compatibility Notes
- [ ] New APIs documented
- [ ] api/next.txt updated
- [ ] Examples added
- [ ] Tests added

## Checklist
- [ ] No unexported type in public API
- [ ] Error types implement error interface
- [ ] Interfaces minimal (Go proverb)
- [ ] Follows existing package conventions
````

## Go Compatibility Promise

From https://go.dev/doc/go1compat:

- Programs written for Go 1.x will work with Go 1.y (y >= x)
- Adding methods to interfaces breaks compatibility
- Adding required struct fields breaks compatibility
- Changing function signatures breaks compatibility

## What You CAN Do

- Add new packages
- Add new functions/methods
- Add new types
- Add new exported constants/variables
- Add methods to concrete types

## What You CANNOT Do

- Remove anything public
- Change signatures
- Add methods to interfaces
- Add fields to exported structs (breaks unkeyed literals)
- Change behavior in breaking ways

## Internal Packages

Packages under `internal/` are NOT subject to compatibility:
- `internal/bytealg`
- `internal/cpu`
- `crypto/internal/*`

## Example Usage

```
/stdlib-compat check encoding/json
/stdlib-compat add-api "func NewDecoder2(io.Reader) *Decoder"
/stdlib-compat review-changes
```
