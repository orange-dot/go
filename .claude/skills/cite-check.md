# Skill: cite-check

Verify citations and references using IEEE format. Essential for academic publications.

## When to Use

- Before submitting papers for publication
- When preparing patent applications
- During literature review
- Any document making external claims

## IEEE Citation Format

### In-Text Citations
```
Single: [1]
Multiple: [1], [2]
Range: [1]-[3]
Combined: [1], [3]-[5], [7]
```

### Reference List Format

**Journal Article:**
```
[1] A. B. Author and C. D. Author, "Article title," Journal Name, vol. X, no. Y, pp. ZZ-ZZ, Month Year.
```

**Conference Paper:**
```
[2] A. B. Author, "Paper title," in Proc. Conference Name, City, Country, Year, pp. ZZ-ZZ.
```

**Book:**
```
[3] A. B. Author, Book Title, Xth ed. City, Country: Publisher, Year.
```

**Standard:**
```
[4] Standard Title, Standard Number, Organization, Year.
```

**Online/Website:**
```
[5] A. Author. "Page title." Website Name. URL (accessed Mon. DD, YYYY).
```

**Datasheet:**
```
[6] Manufacturer, "Part number datasheet," Year. [Online]. Available: URL
```

## What It Checks

### 1. Citation Completeness
- All claims have citations
- No [?] or [citation needed] markers
- Reference list matches in-text citations

### 2. Citation Accuracy
- Source exists and is accessible
- Citation accurately represents source
- Page numbers correct (if specified)

### 3. Format Compliance
- IEEE format followed
- Consistent numbering
- Proper punctuation

### 4. Link Verification
- DOIs resolve correctly
- URLs are accessible
- No broken links

### 5. Source Quality
- Peer-reviewed preferred
- Primary sources over secondary
- Recent sources when relevant

## Output Format

```markdown
# Citation Verification Report

## Document: [filename]

## Summary
- Citations found: N
- Verified: N (%)
- Issues: N (must fix)
- References: N total

## CITATION AUDIT

### Verified ✓
| Ref | In-Text | Source | DOI/URL Status |
|-----|---------|--------|----------------|
| [1] | Lines 23, 45, 67 | IEEE Trans. Power Electron. | DOI ✓ |
| [2] | Line 89 | ISO 15118-20:2022 | Standard ✓ |

### Issues ✗ (MUST FIX)
| Type | Location | Issue | Action Required |
|------|----------|-------|-----------------|
| Missing | Line 34 | Claim uncited | Add source |
| Broken | [5] | DOI not found | Verify DOI |
| Format | [7] | Missing year | Add year |
| Orphan | [12] | Not referenced in text | Remove or use |

### Uncited Claims
| Line | Claim | Suggested Source |
|------|-------|------------------|
| 56 | "Efficiency exceeds 97%" | Measurement report or [calculated] |
| 78 | "Industry standard" | ISO/IEC standard number |

## FORMAT COMPLIANCE

### Correct ✓
[1] J. Smith and A. Jones, "Title here," IEEE Trans. Power Electron., vol. 35, no. 6, pp. 1234-1245, Jun. 2020.

### Needs Correction ✗
| Ref | Issue | Corrected |
|-----|-------|-----------|
| [3] | Missing volume | Add vol. X |
| [8] | URL without access date | Add (accessed Mon. DD, YYYY) |

## REFERENCE LIST
[List all references in IEEE format]
```

## DOI Verification

For DOIs, check:
1. Format: `10.XXXX/XXXXX`
2. Resolution: `https://doi.org/10.XXXX/XXXXX`
3. Matches cited content

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- All technical claims require citations
- Broken links must be fixed or removed
- IEEE format must be followed exactly
- No orphan references allowed

## Source Hierarchy

1. **Preferred:** Peer-reviewed journals, standards (ISO, IEC, IEEE)
2. **Acceptable:** Conference proceedings, technical reports, datasheets
3. **Limited use:** White papers, application notes
4. **Avoid:** Wikipedia, blogs, marketing materials

## Example

```
/cite-check docs/architecture.md
/cite-check docs/invention-disclosure.md
```
