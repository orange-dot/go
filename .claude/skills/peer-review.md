# Skill: peer-review

Simulate academic peer review process. Self-review before submission.

## When to Use

- Before submitting to journals/conferences
- When preparing grant applications
- For internal document review
- To identify weaknesses before external review

## Review Criteria

### 1. Novelty & Contribution
- What is new?
- Is the contribution significant?
- How does it advance the field?

### 2. Technical Soundness
- Are methods appropriate?
- Are results valid?
- Are conclusions supported?

### 3. Clarity & Presentation
- Is writing clear?
- Are figures informative?
- Is structure logical?

### 4. Completeness
- Is related work covered?
- Are limitations discussed?
- Is enough detail provided?

### 5. Significance & Impact
- Who benefits from this work?
- What problems does it solve?
- What are the broader implications?

## Review Output Format

```markdown
# Peer Review Report

## Manuscript: [title]
## Reviewer: Claude (AI-Simulated Review)
## Date: [date]

---

## SUMMARY
[2-3 sentence summary of the manuscript's contribution]

## RECOMMENDATION
☐ Accept
☐ Minor Revision
☐ Major Revision
☐ Reject

## SCORES (1-5 scale)
| Criterion | Score | Comment |
|-----------|-------|---------|
| Novelty | X/5 | |
| Technical soundness | X/5 | |
| Clarity | X/5 | |
| Completeness | X/5 | |
| Significance | X/5 | |
| **Overall** | **X/5** | |

---

## DETAILED REVIEW

### Strengths
1. [Major strength]
2. [Major strength]
3. [Additional strength]

### Weaknesses
1. [Major weakness - must address]
2. [Major weakness - must address]
3. [Minor weakness]

### Questions for Authors
1. [Clarification question]
2. [Technical question]
3. [Scope question]

### Detailed Comments

#### Abstract
- [Comment on abstract quality]

#### Introduction
- [Comment on motivation, context, related work]

#### Methods
- [Comment on methodology]

#### Results
- [Comment on results presentation and validity]

#### Discussion/Conclusion
- [Comment on interpretation and conclusions]

#### Figures/Tables
- [Comment on visual elements]

#### References
- [Comment on citation quality and completeness]

---

## REQUIRED REVISIONS (if Major/Minor Revision)

### Must Address
1. [Critical issue requiring revision]
2. [Critical issue requiring revision]

### Should Address
1. [Important improvement]
2. [Important improvement]

### Consider Addressing
1. [Optional improvement]
2. [Optional improvement]

---

## CONFIDENTIAL COMMENTS TO EDITOR
[Assessment of suitability for venue, ethical concerns, etc.]
```

## Scoring Rubric

### Novelty (1-5)
| Score | Description |
|-------|-------------|
| 5 | Breakthrough, paradigm-shifting |
| 4 | Significant new contribution |
| 3 | Incremental but useful advance |
| 2 | Minor variation of existing work |
| 1 | No novelty, well-known results |

### Technical Soundness (1-5)
| Score | Description |
|-------|-------------|
| 5 | Rigorous, no issues |
| 4 | Sound, minor issues |
| 3 | Mostly sound, some concerns |
| 2 | Significant methodological issues |
| 1 | Fundamental flaws |

### Clarity (1-5)
| Score | Description |
|-------|-------------|
| 5 | Exceptionally clear |
| 4 | Clear, easy to follow |
| 3 | Adequate, some unclear parts |
| 2 | Difficult to follow |
| 1 | Incomprehensible |

## Review Personas

Optionally specify a reviewer persona:

- **`/peer-review strict`** - Highly critical, journal-level scrutiny
- **`/peer-review constructive`** - Balanced, improvement-focused
- **`/peer-review quick`** - High-level feedback only

## STRICT Mode Behavior

This skill operates in **STRICT** mode:
- No flattery or empty praise
- All weaknesses explicitly stated
- Concrete revision requirements
- Honest assessment of publishability

## What Makes a Strong Review

1. **Specific** - Points to exact issues, not vague complaints
2. **Actionable** - Clear path to address each issue
3. **Fair** - Acknowledges strengths alongside weaknesses
4. **Constructive** - Aims to improve the work
5. **Honest** - Does not sugarcoat fundamental problems

## Example

```
/peer-review docs/architecture.md
/peer-review docs/invention-disclosure.md
/peer-review strict docs/paper-draft.md
```
