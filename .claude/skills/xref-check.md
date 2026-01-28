# Skill: xref-check

Validate cross-references within documents. Catches broken links and missing targets.

## When to Use

- Before publication
- After restructuring documents
- When merging content
- After renumbering figures/tables
- During final review

## What It Checks

### 1. Figure References
- "Figure 1" exists
- "Fig. 1" consistent format
- Figures referenced in order
- All figures referenced at least once

### 2. Table References
- "Table 1" exists
- Tables referenced before appearing
- All tables referenced

### 3. Equation References
- "Equation (1)" or "Eq. (1)" exists
- Consistent numbering format
- Variables defined before use

### 4. Section References
- "Section 2.3" exists
- Section numbers match structure
- No references to removed sections

### 5. Citation References
- "[1]" has corresponding reference
- References numbered in order of appearance
- No orphan references

### 6. Internal Links (Markdown)
- `[text](#anchor)` targets exist
- `[text](file.md)` files exist
- Relative paths resolve correctly

## Output Format

```markdown
# Cross-Reference Validation Report

## Document: [filename]
## Date: [date]

---

## SUMMARY

| Type | References | Valid | Broken | Orphan |
|------|------------|-------|--------|--------|
| Figures | N | N | N | N |
| Tables | N | N | N | N |
| Equations | N | N | N | N |
| Sections | N | N | N | N |
| Citations | N | N | N | N |
| Links | N | N | N | N |

**Status:** ✓ All valid / ✗ N issues found

---

## FIGURE REFERENCES

### Valid ✓
| Reference | Line | Target | Status |
|-----------|------|--------|--------|
| Figure 1 | 45 | Line 89 | ✓ |
| Fig. 2 | 67 | Line 134 | ✓ |

### Broken ✗
| Reference | Line | Issue |
|-----------|------|-------|
| Figure 5 | 123 | No Figure 5 exists |
| Fig. 3 | 145 | Only 2 figures in document |

### Orphan Figures (Never Referenced)
| Figure | Line | Action |
|--------|------|--------|
| Figure 4 | 200 | Add reference or remove |

### Format Inconsistencies
| Line | Found | Expected |
|------|-------|----------|
| 45 | "Figure 1" | "Fig. 1" (abbreviate) |
| 67 | "figure 1" | "Fig. 1" (capitalize) |

---

## TABLE REFERENCES

### Valid ✓
| Reference | Line | Target |
|-----------|------|--------|
| Table 1 | 34 | Line 78 |

### Broken ✗
| Reference | Line | Issue |
|-----------|------|-------|
| Table 3 | 156 | Only 2 tables exist |

### Order Issues
| Reference | Line | Table Location | Issue |
|-----------|------|----------------|-------|
| Table 2 | 45 | Line 200 | Referenced before appears |

---

## EQUATION REFERENCES

### Valid ✓
| Reference | Line | Target |
|-----------|------|--------|
| Eq. (1) | 56 | Line 50 |

### Broken ✗
| Reference | Line | Issue |
|-----------|------|-------|
| Eq. (5) | 89 | Only 4 equations |

### Undefined Variables
| Variable | First Use | Definition |
|----------|-----------|------------|
| η | Line 45 | Line 78 ✓ |
| P_out | Line 56 | Not defined ✗ |

---

## SECTION REFERENCES

### Valid ✓
| Reference | Line | Target Section |
|-----------|------|----------------|
| Section 2 | 15 | "2. Background" |
| Section 3.1 | 45 | "3.1 Architecture" |

### Broken ✗
| Reference | Line | Issue |
|-----------|------|-------|
| Section 5 | 89 | Only 4 sections exist |
| Section 2.4 | 102 | Section 2 has only 3 subsections |

---

## CITATION REFERENCES

### Valid ✓
| Reference | Line | Bibliography Entry |
|-----------|------|-------------------|
| [1] | 23 | Smith et al., 2023 |
| [2] | 45 | IEEE 15118 |

### Broken ✗
| Reference | Line | Issue |
|-----------|------|-------|
| [7] | 89 | Only 6 references in bibliography |

### Out of Order
| First Ref | Line | Expected |
|-----------|------|----------|
| [3] | 34 | [2] (should be sequential) |

### Orphan References (In Bibliography, Never Cited)
| Reference | Entry | Action |
|-----------|-------|--------|
| [5] | Jones, 2022 | Cite or remove |

---

## INTERNAL LINKS (Markdown)

### Valid ✓
| Link | Line | Target |
|------|------|--------|
| [Architecture](#architecture) | 12 | Line 45 |
| [specs.md](./specs.md) | 34 | File exists |

### Broken ✗
| Link | Line | Issue |
|------|------|-------|
| [old-section](#removed) | 56 | Anchor not found |
| [missing.md](./missing.md) | 78 | File not found |

---

## CONSISTENCY RECOMMENDATIONS

### Reference Format
**Current mix:**
- "Figure 1" (5 times)
- "Fig. 1" (12 times)
- "figure 1" (2 times)

**Recommendation:** Standardize to "Fig. 1" (IEEE style)

### Numbering
**Current:** Figures and tables numbered independently
**Check:** Fig. 1, Fig. 2, Table 1, Table 2 ✓

---

## AUTO-FIX SUGGESTIONS

| Line | Current | Suggested | Type |
|------|---------|-----------|------|
| 45 | "Figure 1" | "Fig. 1" | Format |
| 67 | "figure 1" | "Fig. 1" | Capitalize |
| 89 | "see [5]" | "see [4]" | Renumber |
```

## Reference Style Guide

### IEEE Style
| Element | In Text | At Element |
|---------|---------|------------|
| Figure | "Fig. 1" | "Fig. 1. Caption here." |
| Table | "Table I" | "TABLE I: Caption" |
| Equation | "(1)" or "Eq. (1)" | Right-aligned (1) |
| Section | "Section II" | "II. SECTION NAME" |
| Citation | "[1]" | [1] A. Author... |

### APA Style
| Element | In Text | At Element |
|---------|---------|------------|
| Figure | "Figure 1" | "Figure 1\nCaption" |
| Table | "Table 1" | "Table 1\nCaption" |
| Equation | "Equation 1" | (1) |
| Section | "the Method section" | Heading levels |
| Citation | "(Author, Year)" | Author, A. (Year)... |

## Integration Workflow

```
1. /xref-check document.md           # Find broken refs
2. /rigor-fix document.md            # Auto-fix format issues
3. Manual fixes for missing targets
4. /xref-check document.md           # Verify all fixed
```

## Example

```
/xref-check docs/specifications.md
/xref-check --style=IEEE paper-draft.md
/xref-check docs/technical-design.md
```
