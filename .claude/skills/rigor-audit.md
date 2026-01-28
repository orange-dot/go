# Skill: rigor-audit

Comprehensive scientific rigor audit. Master skill combining all checks.

## When to Use

- Before major publications
- For periodic documentation health checks
- When preparing for external review
- Before patent filing
- For critical technical documents

## What It Runs

This skill combines all rigor checks into a single comprehensive audit:

| Check | Purpose | Weight |
|-------|---------|--------|
| `/claim-verify` | Evidence for all claims | 25% |
| `/units-check` | SI unit consistency | 15% |
| `/calc-verify` | Calculation accuracy | 20% |
| `/cite-check` | Citation completeness (IEEE) | 15% |
| `/method-check` | Methodology validity | 15% |
| `/reproduce-check` | Reproducibility | 10% |

## Audit Levels

### Quick Audit (`/rigor-audit quick`)
- claim-verify (critical claims only)
- units-check
- calc-verify

### Standard Audit (`/rigor-audit` or `/rigor-audit standard`)
- All checks except reproduce-check
- Full document scan

### Full Audit (`/rigor-audit full`)
- All 6 checks
- Deep analysis
- Cross-reference validation

## Output Format

```markdown
# Scientific Rigor Audit Report

## Document: [filename]
## Audit Level: Quick/Standard/Full
## Date: [date]

---

## EXECUTIVE SUMMARY

### Overall Rigor Score: X/100

| Category | Score | Status |
|----------|-------|--------|
| Claims & Evidence | X/25 | 🔴/🟡/🟢 |
| Units | X/15 | 🔴/🟡/🟢 |
| Calculations | X/20 | 🔴/🟡/🟢 |
| Citations | X/15 | 🔴/🟡/🟢 |
| Methodology | X/15 | 🔴/🟡/🟢 |
| Reproducibility | X/10 | 🔴/🟡/🟢 |

### Verdict
☐ **Publication Ready** (≥85)
☐ **Minor Issues** (70-84) - Fix before publication
☐ **Major Issues** (50-69) - Significant work needed
☐ **Not Ready** (<50) - Fundamental problems

---

## CRITICAL ISSUES (Must Fix Before Publication)

### 🔴 Blockers
1. [Issue] - [Location] - [Required action]
2. [Issue] - [Location] - [Required action]

### 🟡 Major Issues
1. [Issue] - [Location] - [Required action]
2. [Issue] - [Location] - [Required action]

---

## DETAILED FINDINGS

### Claims & Evidence (X/25)
[Summary from /claim-verify]

**Unverified Claims:**
| Line | Claim | Required |
|------|-------|----------|
| | | |

**Marketing Language Found:**
| Line | Text | Replace With |
|------|------|--------------|
| | | |

### Units (X/15)
[Summary from /units-check]

**Unit Errors:**
| Line | Found | Correct |
|------|-------|---------|
| | | |

### Calculations (X/20)
[Summary from /calc-verify]

**Calculation Errors:**
| Line | Stated | Calculated | Error |
|------|--------|------------|-------|
| | | | |

### Citations (X/15)
[Summary from /cite-check]

**Citation Issues:**
| Ref | Issue | Action |
|-----|-------|--------|
| | | |

### Methodology (X/15)
[Summary from /method-check]

**Methodology Gaps:**
| Gap | Severity | Recommendation |
|-----|----------|----------------|
| | | |

### Reproducibility (X/10)
[Summary from /reproduce-check]

**Reproducibility Level:** RX
**Missing for R3:**
- [ ] Item
- [ ] Item

---

## ACTION ITEMS

### Immediate (Before Any Sharing)
1. [ ] Fix [critical issue]
2. [ ] Fix [critical issue]

### Before Publication
1. [ ] Address [major issue]
2. [ ] Address [major issue]

### Recommended Improvements
1. [ ] Consider [enhancement]
2. [ ] Consider [enhancement]

---

## AUDIT METADATA

- Auditor: Claude Code (rigor-audit skill)
- Documents scanned: N
- Lines analyzed: N
- Time: [timestamp]
- Version: 1.0
```

## Scoring Guide

### Category Scoring

**Claims & Evidence (25 points)**
- 25: All claims verified, no marketing language
- 20: Minor gaps, no critical issues
- 15: Some unverified claims
- 10: Significant unverified content
- 5: Mostly unverified
- 0: No evidence provided

**Units (15 points)**
- 15: Perfect SI compliance
- 12: Minor capitalization issues
- 9: Some unit errors
- 6: Frequent errors
- 3: Systematic problems
- 0: Dimensional errors present

**Calculations (20 points)**
- 20: All calculations verified correct
- 16: Minor rounding differences
- 12: Some errors, non-critical
- 8: Critical calculation errors
- 4: Multiple critical errors
- 0: Fundamental errors

**Citations (15 points)**
- 15: Complete, correct IEEE format
- 12: Minor format issues
- 9: Missing some citations
- 6: Significant citation gaps
- 3: Poor citation practice
- 0: No citations

**Methodology (15 points)**
- 15: Rigorous, peer-review ready
- 12: Sound with minor gaps
- 9: Adequate methodology
- 6: Significant gaps
- 3: Weak methodology
- 0: No valid methodology

**Reproducibility (10 points)**
- 10: R4 level (full reproduction)
- 8: R3 level (partial reproduction)
- 5: R2 level (method reproduction)
- 2: R1 level (conceptual only)
- 0: Not reproducible

## STRICT Mode Behavior

This skill operates in **STRICT** mode:
- Score <85 = not publication ready
- All blockers must be resolved
- No exceptions for "minor" issues in critical documents

## Example

```
/rigor-audit tehnika/inzenjersko/en/SPECIFICATIONS.md
/rigor-audit full patent/01-IP-FOUNDATION/
/rigor-audit quick research/preliminary-results.md
```
