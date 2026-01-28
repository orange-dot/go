# Skill: fig-review

Review figures, tables, and visual elements for quality. Essential for publication.

## When to Use

- Before paper submission
- When creating technical documentation
- For presentation materials
- During design reviews
- Patent application figures

## Figure Quality Criteria

### 1. Clarity
- Purpose immediately clear
- Self-explanatory with caption
- Appropriate level of detail
- No unnecessary elements

### 2. Legibility
- Text readable at print size
- Sufficient contrast
- Appropriate line weights
- Clear labels

### 3. Consistency
- Uniform style across figures
- Consistent color scheme
- Matching fonts
- Aligned with text style

### 4. Accuracy
- Data correctly represented
- Axes properly labeled
- Units included
- Scale appropriate

### 5. Accessibility
- Color-blind friendly
- Patterns for distinction
- Alt text for digital
- High-contrast options

## Table Quality Criteria

### 1. Structure
- Clear headers
- Logical organization
- Appropriate row/column count
- No merged cells (if avoidable)

### 2. Content
- Units in headers (not cells)
- Consistent decimal places
- Meaningful comparisons
- No redundant data

### 3. Formatting
- Aligned numbers (decimal/right)
- Minimal gridlines
- Adequate spacing
- Bold for emphasis (sparingly)

## Output Format

```markdown
# Figure & Table Review Report

## Document: [filename]
## Date: [date]

---

## SUMMARY

| Element Type | Count | Issues | Score |
|--------------|-------|--------|-------|
| Figures | N | N | X/10 |
| Tables | N | N | X/10 |
| Equations | N | N | X/10 |
| **Overall** | | | **X/10** |

---

## FIGURE REVIEW

### Figure 1: [Title/Description]
**Location:** Line X / Page Y
**Purpose:** [What it shows]

| Criterion | Score | Notes |
|-----------|-------|-------|
| Clarity | X/5 | |
| Legibility | X/5 | |
| Accuracy | X/5 | |
| Caption | X/5 | |

**Issues:**
- [ ] [Issue 1]
- [ ] [Issue 2]

**Recommendations:**
- [Specific improvement 1]
- [Specific improvement 2]

---

### Figure 2: [Title/Description]
[Same format...]

---

## TABLE REVIEW

### Table 1: [Title/Description]
**Location:** Line X / Page Y
**Purpose:** [What it presents]

| Criterion | Score | Notes |
|-----------|-------|-------|
| Structure | X/5 | |
| Headers | X/5 | |
| Data format | X/5 | |
| Caption | X/5 | |

**Issues:**
- [ ] [Issue 1]

**Recommendations:**
- [Specific improvement]

---

## EQUATION REVIEW

### Equation 1: [Description]
**Location:** Line X
**Numbered:** Yes/No

| Check | Status |
|-------|--------|
| Formatted correctly | ✓/✗ |
| Variables defined | ✓/✗ |
| Units consistent | ✓/✗ |
| Referenced in text | ✓/✗ |

---

## CROSS-REFERENCE CHECK

| Reference | Target | Status |
|-----------|--------|--------|
| "Figure 1" | Fig. 1 exists | ✓ |
| "Table 3" | Table 3 missing | ✗ |
| "Eq. (5)" | Only 4 equations | ✗ |

---

## STYLE CONSISTENCY

### Figures
| Aspect | Consistent? | Values Found |
|--------|-------------|--------------|
| Font | ✓/✗ | Arial, Times |
| Colors | ✓/✗ | Blue, #0066CC |
| Line weight | ✓/✗ | 1pt, 2pt |
| Labels | ✓/✗ | (a), a), A |

### Tables
| Aspect | Consistent? | Values Found |
|--------|-------------|--------------|
| Header style | ✓/✗ | Bold, regular |
| Alignment | ✓/✗ | Left, center |
| Borders | ✓/✗ | Full, minimal |

---

## ACCESSIBILITY CHECK

| Figure | Color-blind safe | Alt text | Pattern distinction |
|--------|------------------|----------|---------------------|
| Fig. 1 | ✓/✗ | ✓/✗ | N/A |
| Fig. 2 | ✓/✗ | ✓/✗ | ✓/✗ |

---

## CAPTION QUALITY

### Good Captions ✓
- Fig. 3: "System architecture showing module interconnections and control flow. Solid lines indicate power flow; dashed lines indicate communication."

### Needs Improvement ✗
- Fig. 1: "The system" → Too vague
- Table 2: Missing entirely

### Caption Checklist
- [ ] Describes what is shown
- [ ] Explains symbols/abbreviations
- [ ] Self-contained (readable without text)
- [ ] Ends with period
```

## Figure Type Guidelines

### Block Diagrams
- Clear hierarchy
- Consistent block sizes
- Labeled connections
- Legend if needed

### Data Plots
- Axis labels with units
- Legend (if multiple series)
- Error bars (if applicable)
- Data points visible (if few)

### Photographs
- High resolution
- Scale bar included
- Key features labeled
- Consistent lighting

### Schematics
- Standard symbols
- Clear signal flow
- Component values
- Reference designators

## Table Type Guidelines

### Comparison Tables
- Reference/baseline clearly marked
- Best values highlighted
- Consistent metrics
- Fair comparison

### Specification Tables
- Parameter | Symbol | Value | Unit
- Grouped logically
- Typical and max values
- Conditions noted

### Results Tables
- Statistical measures included
- Uncertainty indicated
- Significant figures appropriate

## Publication-Specific Rules

### IEEE
- Figure captions below
- Table captions above
- "Fig." abbreviated in text
- Equations numbered (right)

### Elsevier
- Color figures extra cost (print)
- High-res requirements (300+ DPI)
- Vector preferred

### Patent Figures
- Black and white only (typically)
- Reference numerals required
- Brief description separately

## Example

```
/fig-review tehnika/inzenjersko/en/SPECIFICATIONS.md
/fig-review patent/03-TECHNICAL-SUPPORT/EK3-DETAILED-DESIGN.md
/fig-review --format=IEEE paper-draft.md
```
