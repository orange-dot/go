# Skill: claim-verify

Audit technical claims for supporting evidence. Foundation skill for scientific rigor.

## When to Use

- Before publishing technical documentation
- When reviewing research claims
- During patent disclosure preparation
- Any document making quantitative assertions

## Claim Classification

Every technical claim must be tagged with its evidence type:

| Tag | Meaning | Requirement |
|-----|---------|-------------|
| `[measured]` | Empirical data | Lab/field measurements, with methodology |
| `[calculated]` | Derived value | Formula shown, inputs specified |
| `[cited]` | External source | IEEE citation [n] required |
| `[assumed]` | Assumption | Must be explicitly stated |
| `[estimated]` | Engineering estimate | Basis for estimate documented |

## What It Checks

### 1. Unsupported Claims
- Quantitative values without source
- Performance assertions without evidence
- Comparisons without baseline

### 2. Marketing Language (STRICT: Must be removed)
- "World-class", "best-in-class", "revolutionary"
- "Significant improvement" without quantification
- Superlatives without comparison data

### 3. Hidden Assumptions
- Implicit environmental conditions
- Unstated boundary conditions
- Default values not documented

### 4. Claim-Evidence Mismatch
- Source doesn't support claim
- Extrapolation beyond data
- Cherry-picked references

## Output Format

```markdown
# Claim Verification Report

## Document: [filename]

## Summary
- Total claims analyzed: N
- Verified: N (%)
- Unverified: N (%)
- Assumptions identified: N

## Claim Audit

### VERIFIED ✓
| Line | Claim | Evidence Type | Source |
|------|-------|---------------|--------|
| 42 | "Efficiency 97.5%" | [measured] | Test report TR-2024-001 |

### UNVERIFIED ✗ (MUST FIX)
| Line | Claim | Issue | Required Action |
|------|-------|-------|-----------------|
| 87 | "Best performance" | Marketing language | Remove or quantify |
| 103 | "500kW output" | No source | Add [calculated] or [cited] |

### ASSUMPTIONS IDENTIFIED
| Line | Assumption | Impact | Recommendation |
|------|------------|--------|----------------|
| 15 | "Ambient 25°C" | Thermal calculations | Document explicitly |

## Severity Summary
- 🔴 Critical (blocks publication): N
- 🟡 Major (should fix): N
- 🟢 Minor (consider fixing): N
```

## Steps

1. Parse document for quantitative claims
2. Identify evidence tags (or lack thereof)
3. Check marketing language patterns
4. Trace citations to sources
5. Flag hidden assumptions
6. Generate severity-ranked report

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- All unverified claims are errors, not warnings
- Marketing language must be removed or replaced
- Every assumption must be documented
- No publication until all critical issues resolved

## Example

```
/claim-verify docs/specifications.md
/claim-verify docs/invention-disclosure.md
```
