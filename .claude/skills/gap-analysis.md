# Skill: gap-analysis

Identify gaps in literature, technology, or market. Foundation for research contribution.

## When to Use

- Starting new research project
- Writing grant proposals
- Identifying innovation opportunities
- Competitive analysis
- Patent landscaping

## Gap Types

### 1. Knowledge Gaps
What is not yet known or understood
- Unexplored phenomena
- Conflicting findings unresolved
- Missing theoretical frameworks

### 2. Methodology Gaps
Limitations in current approaches
- Inadequate measurement methods
- Unvalidated assumptions
- Missing benchmarks

### 3. Technology Gaps
What cannot yet be built or achieved
- Performance limitations
- Missing components
- Integration challenges

### 4. Application Gaps
Where solutions haven't been applied
- New use cases
- Underserved markets
- Scaling challenges

### 5. Standardization Gaps
Missing or inadequate standards
- No interoperability specs
- Incomplete test procedures
- Conflicting standards

## Output Format

```markdown
# Gap Analysis Report

## Domain: [field/topic]
## Date: [date]

---

## EXECUTIVE SUMMARY

### Key Gaps Identified
1. [Gap 1] - High priority
2. [Gap 2] - Medium priority
3. [Gap 3] - Opportunity

### Recommended Focus Areas
- [Area 1]: Highest impact potential
- [Area 2]: Low competition
- [Area 3]: Alignment with capabilities

---

## CURRENT STATE OF THE ART

### What Exists
| Area | State | Key Players | Maturity |
|------|-------|-------------|----------|
| [Area 1] | [Description] | [Companies/Groups] | High/Med/Low |
| [Area 2] | [Description] | [Companies/Groups] | High/Med/Low |

### What Works Well
- [Strength 1]
- [Strength 2]

### Known Limitations
- [Limitation 1]
- [Limitation 2]

---

## KNOWLEDGE GAPS

### Gap K1: [Title]
**Description:** [What is unknown]

**Evidence:**
- Literature search found N papers, none addressing X
- Conflicting claims: Author A says X, Author B says Y
- Theoretical basis missing for [phenomenon]

**Impact if Filled:**
- Would enable [capability]
- Would resolve [conflict]

**Research Required:**
- [ ] [Research task 1]
- [ ] [Research task 2]

**Difficulty:** High/Medium/Low
**Priority:** High/Medium/Low

---

### Gap K2: [Title]
[Same structure...]

---

## TECHNOLOGY GAPS

### Gap T1: [Title]
**Description:** [What cannot be achieved]

**Current Limitation:**
- Best reported: [metric = value]
- Required: [metric = target value]
- Gap: [difference]

**Blocking Factors:**
1. [Technical barrier 1]
2. [Technical barrier 2]

**Potential Solutions:**
| Approach | Feasibility | Risk | Timeline |
|----------|-------------|------|----------|
| [Approach 1] | High/Med/Low | High/Med/Low | [time] |
| [Approach 2] | High/Med/Low | High/Med/Low | [time] |

**Priority:** High/Medium/Low

---

## APPLICATION GAPS

### Gap A1: [Title]
**Description:** [Where solution hasn't been applied]

**Opportunity:**
- Market size: [estimate]
- Current solutions: [none / inadequate]
- Barriers to entry: [list]

**Requirements for Success:**
1. [Requirement 1]
2. [Requirement 2]

**Priority:** High/Medium/Low

---

## STANDARDIZATION GAPS

### Gap S1: [Title]
**Description:** [Missing or inadequate standard]

**Current State:**
- Relevant standards: [list existing]
- Coverage: [what's covered]
- Missing: [what's not covered]

**Impact:**
- Interoperability issues
- Certification barriers
- Market fragmentation

**Recommended Action:**
- Engage with [standards body]
- Propose [specific standard work]

---

## GAP PRIORITIZATION MATRIX

| Gap | Impact | Feasibility | Competition | Priority |
|-----|--------|-------------|-------------|----------|
| K1 | High | Medium | Low | ⭐⭐⭐ |
| T1 | High | Low | High | ⭐⭐ |
| A1 | Medium | High | Medium | ⭐⭐⭐ |
| S1 | Medium | Medium | Low | ⭐⭐ |

### Scoring Criteria
- **Impact:** Value if gap is filled
- **Feasibility:** Technical/resource feasibility
- **Competition:** How many others are working on it

---

## RECOMMENDED RESEARCH AGENDA

### Phase 1: Foundation (0-6 months)
1. Address Gap K1: [Brief description]
2. Initial work on Gap T1: [Brief description]

### Phase 2: Development (6-18 months)
1. Continue Gap T1
2. Begin Gap A1 exploration

### Phase 3: Application (18-36 months)
1. Deploy findings from K1, T1
2. Address Gap S1 through standardization

---

## COMPETITIVE LANDSCAPE

### Who's Working on What
| Gap | Active Researchers/Companies | Our Advantage |
|-----|------------------------------|---------------|
| K1 | [Names] | [Our unique angle] |
| T1 | [Names] | [Our unique angle] |

### White Space Opportunities
1. [Area with little activity]
2. [Intersection not being explored]

---

## SOURCES REVIEWED

- [N] academic papers
- [N] patents
- [N] technical reports
- [N] industry publications
```

## Gap Identification Methods

### Literature-Based
1. Systematic review of field
2. Identify "future work" sections
3. Find conflicting results
4. Note methodological critiques

### Technology-Based
1. Compare specs to requirements
2. Identify bottlenecks in systems
3. Find missing integration points
4. Benchmark against theoretical limits

### Market-Based
1. Customer pain points
2. Unserved segments
3. Geographic gaps
4. Use case gaps

### Expert-Based
1. Interview domain experts
2. Conference/workshop discussions
3. Review grant calls (what's funded = perceived gap)
4. Patent landscaping

## Integration with Other Skills

```
1. /lit-search [topic]              # Gather state of the art
2. /prior-art [topic]               # Check patent landscape
3. /gap-analysis                    # Identify opportunities
4. /hypothesis-gen                  # Convert gaps to hypotheses
5. /paper-structure                 # Structure research proposal
```

## Example

```
/gap-analysis "modular EV charging systems"
/gap-analysis "V2G bidirectional power conversion"
/gap-analysis --type=technology "SiC power modules for automotive"
```
