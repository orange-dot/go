# Skill: rigor-fix

Automatically implement fixes from rigor audit reports. Completes the audit-fix workflow.

## When to Use

- After running `/rigor-audit`
- After running any individual rigor check
- When audit report shows fixable issues
- To batch-fix documentation problems

## What It Fixes

### From `/claim-verify`
| Issue | Auto-Fix Action |
|-------|-----------------|
| Marketing language | Remove or replace with quantified claims |
| Missing evidence tag | Add `[assumed]` or prompt for source |
| Unsupported claim | Flag for manual review or remove |

### From `/units-check`
| Issue | Auto-Fix Action |
|-------|-----------------|
| KW → kW | Auto-correct capitalization |
| Missing space | Add space (150kW → 150 kW) |
| Non-SI units | Convert with note |

### From `/calc-verify`
| Issue | Auto-Fix Action |
|-------|-----------------|
| Arithmetic error | Recalculate and update |
| Input mismatch | Flag with correct value from spec |
| Missing calculation | Add derivation |

### From `/cite-check`
| Issue | Auto-Fix Action |
|-------|-----------------|
| Format error | Reformat to IEEE |
| Missing citation | Add `[citation needed]` marker |
| Orphan reference | Remove or add in-text citation |

### From `/method-check`
| Issue | Auto-Fix Action |
|-------|-----------------|
| Missing limitation | Add limitations section stub |
| Unstated assumption | Add assumptions section |

## Output Format

```markdown
# Rigor Fix Report

## Source: [audit report or document]
## Date: [date]

---

## SUMMARY
- Issues identified: N
- Auto-fixed: N
- Manual review required: N
- Skipped (unfixable): N

---

## FIXES APPLIED

### Units Corrections
| File | Line | Before | After |
|------|------|--------|-------|
| spec.md | 45 | "150 KW" | "150 kW" |
| spec.md | 78 | "3.3kW" | "3.3 kW" |

### Calculation Corrections
| File | Line | Parameter | Old Value | New Value | Derivation |
|------|------|-----------|-----------|-----------|------------|
| spec.md | 102 | Module count | 145 | 152 | ceil(500/3.3) |

### Citation Format Corrections
| File | Ref | Issue | Corrected |
|------|-----|-------|-----------|
| arch.md | [3] | Missing year | Added "2024" |

### Marketing Language Removed
| File | Line | Removed | Replacement |
|------|------|---------|-------------|
| intro.md | 23 | "world-class" | [deleted] |
| intro.md | 45 | "revolutionary" | "novel" |

---

## MANUAL REVIEW REQUIRED

### Claims Needing Sources
| File | Line | Claim | Action Needed |
|------|------|-------|---------------|
| spec.md | 67 | "Efficiency 97.5%" | Add measurement reference |

### Assumptions to Document
| File | Line | Implicit Assumption | Suggested Text |
|------|------|---------------------|----------------|
| thermal.md | 34 | Ambient temperature | "Assuming T_amb = 25°C" |

### Methodology Gaps
| File | Section | Gap | Suggested Addition |
|------|---------|-----|-------------------|
| test.md | Methods | No uncertainty | Add "±X%" to measurements |

---

## UNFIXABLE ISSUES (Require Rewrite)
1. [file:line] - Fundamental claim unsupported
2. [file:line] - Methodology invalid

---

## VERIFICATION
After fixes applied:
- Run `/units-check` - Expected: 0 errors
- Run `/calc-verify` - Expected: 0 errors
- Run `/cite-check` - Expected: format issues resolved

## FILES MODIFIED
- spec.md (12 changes)
- arch.md (3 changes)
- thermal.md (5 changes)
```

## Fix Modes

### Safe Mode (Default)
```
/rigor-fix
```
- Only auto-fix unambiguous issues (units, formatting)
- Flag everything else for review
- No content deletion without confirmation

### Aggressive Mode
```
/rigor-fix aggressive
```
- Auto-fix all fixable issues
- Remove unsupported claims
- Add placeholder sections
- May require more manual review after

### Preview Mode
```
/rigor-fix preview
```
- Show what would be fixed
- No actual changes made
- Use to review before applying

## Integration Workflow

```
1. /rigor-audit full document.md     # Find issues
2. /rigor-fix preview document.md    # Preview fixes
3. /rigor-fix document.md            # Apply fixes
4. /rigor-audit document.md          # Verify fixes
```

## Safety Rules

1. **Never delete substantive content** without explicit flag
2. **Preserve original meaning** when rewording
3. **Track all changes** for review
4. **Create backup** before bulk fixes
5. **Verify after fixing** with re-audit

## Example

```
/rigor-fix tehnika/inzenjersko/en/SPECIFICATIONS.md
/rigor-fix aggressive patent/01-IP-FOUNDATION/
/rigor-fix preview research/paper-draft.md
```
