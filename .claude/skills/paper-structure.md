# Skill: paper-structure

Structure documents following academic conventions. Essential for publication readiness.

## When to Use

- Converting technical docs to papers
- Organizing research findings
- Preparing grant proposals
- Structuring technical reports
- Reviewing document organization

## Standard Structures

### IMRaD (Scientific Papers)
```
1. Introduction
   - Context/background
   - Problem statement
   - Contribution summary

2. Methods
   - Experimental setup
   - Procedures
   - Analysis methods

3. Results
   - Findings (no interpretation)
   - Data presentation
   - Statistical analysis

4. Discussion
   - Interpretation
   - Comparison to prior work
   - Limitations
   - Future work

5. Conclusion
   - Summary of contribution
   - Key takeaways
```

### Engineering Paper Structure
```
1. Introduction
2. Related Work / Background
3. System Architecture / Design
4. Implementation
5. Evaluation / Experiments
6. Discussion
7. Conclusion
```

### Patent Application Structure
```
1. Title
2. Abstract
3. Field of Invention
4. Background
5. Summary of Invention
6. Brief Description of Drawings
7. Detailed Description
8. Claims
```

### Technical Report Structure
```
1. Executive Summary
2. Introduction
3. Technical Approach
4. Results
5. Conclusions
6. Recommendations
7. Appendices
```

## Output Format

```markdown
# Document Structure Analysis

## Document: [filename]
## Target Format: [IMRaD/Engineering/Patent/Report]

---

## CURRENT STRUCTURE

### Sections Found
1. [Section name] - Lines X-Y
2. [Section name] - Lines X-Y
...

### Structure Score: X/10
- Completeness: X/5
- Organization: X/5

---

## RECOMMENDED STRUCTURE

### Proposed Outline
```
1. Introduction (current: Section 2, partial)
   1.1 Context [MISSING]
   1.2 Problem Statement [from line 45]
   1.3 Contributions [MISSING]

2. Background (current: Section 1)
   2.1 Related Work [from lines 12-44]
   2.2 Technical Foundations [MISSING]

3. System Design (current: scattered)
   3.1 Architecture [from lines 89-120]
   3.2 Module Design [from lines 150-200]
   ...
```

### Content Mapping
| Recommended Section | Current Location | Action |
|--------------------|------------------|--------|
| 1. Introduction | Lines 45-60 | Expand |
| 2.1 Related Work | Lines 12-44 | Move |
| 3.1 Architecture | Lines 89-120 | Keep |
| 4. Evaluation | MISSING | Write |

---

## MISSING ELEMENTS

### Critical (Must Add)
1. **Contributions statement** - List 3-5 specific contributions
2. **Evaluation section** - Quantitative validation
3. **Limitations** - Honest assessment of scope

### Recommended
1. Related work comparison table
2. Future work section
3. Notation table

---

## SECTION TEMPLATES

### Introduction Template
```markdown
## 1. Introduction

[Opening hook - why this matters]

[Context - 1-2 paragraphs on background]

[Problem statement - specific gap being addressed]

[Approach summary - how you address the problem]

**Contributions.** This paper makes the following contributions:
- [Contribution 1]
- [Contribution 2]
- [Contribution 3]

[Paper organization - "Section 2 presents..."]
```

### Related Work Template
```markdown
## 2. Related Work

### 2.1 [Category 1]
[Discussion of prior work in category 1]

### 2.2 [Category 2]
[Discussion of prior work in category 2]

### 2.3 Comparison Summary
| Approach | Feature A | Feature B | Feature C |
|----------|-----------|-----------|-----------|
| Prior 1  | ✓         | ✗         | ✓         |
| Prior 2  | ✗         | ✓         | ✗         |
| **Ours** | **✓**     | **✓**     | **✓**     |
```

### Evaluation Template
```markdown
## 5. Evaluation

### 5.1 Experimental Setup
[Hardware, software, parameters]

### 5.2 Metrics
[What you measure and why]

### 5.3 Results
[Data with analysis]

### 5.4 Comparison
[vs. baselines/prior work]
```

---

## TRANSITIONS NEEDED

| From | To | Suggested Transition |
|------|-----|---------------------|
| Intro | Background | "Before presenting our approach, we review..." |
| Background | Design | "Building on these foundations, we now present..." |
| Design | Implementation | "We implemented this design as follows..." |
| Results | Discussion | "These results demonstrate..." |

---

## FIGURE/TABLE PLACEMENT

### Current
- Figure 1: Line 89 (architecture) ✓
- Table 1: Line 234 (specs) - Move to Section 3

### Recommended
| Element | Recommended Section | Purpose |
|---------|---------------------|---------|
| Fig. 1 | Introduction | Overview |
| Fig. 2 | System Design | Architecture |
| Fig. 3 | Evaluation | Results plot |
| Table 1 | System Design | Specifications |
| Table 2 | Evaluation | Comparison |
```

## Flow Checklist

### Logical Flow
- [ ] Each section builds on previous
- [ ] No forward references to undefined concepts
- [ ] Transitions between sections are smooth
- [ ] Consistent level of detail throughout

### Reader Guidance
- [ ] Roadmap in introduction
- [ ] Section previews where needed
- [ ] Clear topic sentences
- [ ] Summary/transition at section ends

## Integration with Other Skills

```
1. /paper-structure document.md      # Analyze structure
2. Apply restructuring
3. /claim-verify document.md         # Check claims
4. /cite-check document.md           # Check citations
5. /peer-review document.md          # Full review
```

## Example

```
/paper-structure tehnika/konceptualno/en/00-arhitektura.md
/paper-structure --format=IEEE patent/01-IP-FOUNDATION/01-01-invention-disclosure-modular.md
/paper-structure --format=patent disclosure.md
```
