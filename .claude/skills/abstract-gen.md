# Skill: abstract-gen

Generate structured abstracts from document content. Essential for academic output.

## When to Use

- Writing journal/conference papers
- Creating executive summaries
- Patent application abstracts
- Grant proposal summaries
- Technical report abstracts

## Abstract Types

### 1. Structured Abstract (Preferred for Technical)
```
Background: [Context and motivation]
Objective: [What this work addresses]
Methods: [Approach taken]
Results: [Key findings with numbers]
Conclusions: [Significance and implications]
```

### 2. Unstructured Abstract (Traditional)
Single paragraph covering all elements in flowing prose.

### 3. Graphical Abstract
Key figure + 2-3 sentence summary.

### 4. Patent Abstract
Technical description of invention without claims language.

## Abstract Requirements by Venue

| Venue Type | Length | Style | Key Elements |
|------------|--------|-------|--------------|
| IEEE Journal | 150-250 words | Structured optional | Results emphasis |
| IEEE Conference | 100-150 words | Unstructured | Contribution focus |
| Nature/Science | 150 words max | Unstructured | Significance first |
| Patent (USPTO) | 150 words max | Technical | No claims language |
| Grant (NSF) | 1 page | Structured | Broader impacts |

## Output Format

```markdown
# Abstract Generation Report

## Document: [filename]
## Type: [journal/conference/patent/grant]
## Target Length: [N words]

---

## EXTRACTED KEY ELEMENTS

### Background/Context
- [Key context point 1]
- [Key context point 2]

### Problem/Gap
- [Problem statement]

### Objective/Contribution
- [Main objective]
- [Specific contributions]

### Methods/Approach
- [Method 1]
- [Method 2]

### Results/Findings
- [Quantitative result 1]
- [Quantitative result 2]

### Conclusions/Significance
- [Main conclusion]
- [Broader impact]

---

## GENERATED ABSTRACTS

### Structured Abstract (IEEE Style)
**Background:** [1-2 sentences on context]
**Objective:** [1 sentence on goal]
**Methods:** [2-3 sentences on approach]
**Results:** [2-3 sentences with numbers]
**Conclusions:** [1-2 sentences on significance]

*Word count: N*

### Unstructured Abstract
[Single flowing paragraph version]

*Word count: N*

### Short Abstract (Conference/Social)
[2-3 sentence version for limited space]

*Word count: N*

---

## KEYWORDS
[keyword1], [keyword2], [keyword3], [keyword4], [keyword5]

## QUALITY CHECKLIST
- [ ] States the problem clearly
- [ ] Describes methodology briefly
- [ ] Includes quantitative results
- [ ] Conveys significance
- [ ] Self-contained (no citations)
- [ ] No undefined acronyms
- [ ] Within word limit
```

## Abstract Quality Criteria

### Must Include
1. **Problem/motivation** - Why does this matter?
2. **Approach** - What did you do?
3. **Key results** - What did you find? (with numbers!)
4. **Significance** - Why should readers care?

### Must Avoid
1. **Vague claims** - "significant improvement" without numbers
2. **Citations** - Abstract must be self-contained
3. **Undefined acronyms** - Spell out on first use
4. **Future tense** - Results should be stated as facts
5. **First person** - Typically avoid "we" in abstracts

## Writing Patterns

### Strong Opening Lines
```
✓ "This paper presents a modular 3.3 kW power module for EV charging..."
✓ "We demonstrate a 97.5% efficient bidirectional converter..."
✓ "A novel distributed control algorithm enables..."

✗ "This paper is about EV charging..."
✗ "In recent years, EV adoption has increased..."
✗ "The authors present..."
```

### Strong Results Statements
```
✓ "achieves 97.5% peak efficiency at 3.3 kW"
✓ "reduces system cost by 34% compared to..."
✓ "enables hot-swap within 2.3 seconds"

✗ "shows good efficiency"
✗ "significantly reduces cost"
✗ "enables fast operation"
```

### Strong Conclusions
```
✓ "enables scalable deployment from 3 kW to 3 MW"
✓ "provides a foundation for grid-integrated charging"
✓ "demonstrates feasibility for commercial bus depots"

✗ "could be useful for EV charging"
✗ "shows promising results"
✗ "may have applications"
```

## Patent Abstract Special Rules

For USPTO:
- Must not use claims language ("comprising", "wherein")
- Focus on technical description
- Reference key drawing figure
- 150 words maximum
- No marketing language

```markdown
### Patent Abstract
A modular power conversion system for electric vehicle charging
comprising stackable 3.3 kW modules (10). Each module includes
a bidirectional DC-DC converter (12) with silicon carbide
switching devices (14) and an integrated thermal management
system (16). Modules communicate via CAN-FD bus (18) for
distributed coordination. The system scales from single-module
residential use to multi-megawatt depot installations through
parallel module aggregation. Hot-swap capability enables
field replacement without system shutdown. [Fig. 1]
```

## Integration Workflow

```
1. Write full document
2. /abstract-gen document.md           # Generate abstract
3. /claim-verify abstract.md           # Verify no unsupported claims
4. /peer-review abstract.md            # Quality check
```

## Example

```
/abstract-gen tehnika/konceptualno/en/00-arhitektura.md
/abstract-gen patent/01-IP-FOUNDATION/01-01-invention-disclosure-modular.md
/abstract-gen --type=conference --length=150 paper.md
```
