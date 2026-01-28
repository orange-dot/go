# Skill: hypothesis-gen

Generate testable hypotheses from observations and data. Foundation for research methodology.

## When to Use

- Starting new research direction
- Analyzing unexpected results
- Converting observations to research questions
- Planning experiments
- Grant proposal development

## Hypothesis Quality Criteria

### SMART Hypotheses
| Criterion | Description | Example |
|-----------|-------------|---------|
| **S**pecific | Clear, focused statement | "Efficiency increases with..." |
| **M**easurable | Quantifiable outcome | "...by at least 5%..." |
| **A**chievable | Testable with available resources | "...under lab conditions..." |
| **R**elevant | Addresses real problem | "...improving system cost..." |
| **T**ime-bound | Defined scope | "...for modules <5kW" |

### Hypothesis Types
| Type | Structure | Example |
|------|-----------|---------|
| Null (H₀) | No effect/relationship | "Module count has no effect on efficiency" |
| Alternative (H₁) | Effect exists | "Increasing module count reduces system efficiency" |
| Directional | Specifies direction | "Efficiency decreases as module count increases" |
| Non-directional | Effect without direction | "Module count affects system efficiency" |

## Output Format

```markdown
# Hypothesis Generation Report

## Context: [observation/problem]
## Date: [date]

---

## OBSERVATIONS ANALYZED

### Input Data/Observations
1. [Observation 1]
2. [Observation 2]
3. [Observation 3]

### Patterns Identified
- [Pattern 1]: Correlation between X and Y
- [Pattern 2]: Anomaly in condition Z
- [Pattern 3]: Trend over time/parameter

---

## GENERATED HYPOTHESES

### Hypothesis 1: [Short Title]

**Research Question:**
Does [independent variable] affect [dependent variable] in [context]?

**Null Hypothesis (H₀):**
[Independent variable] has no significant effect on [dependent variable].

**Alternative Hypothesis (H₁):**
[Independent variable] [increases/decreases] [dependent variable] by [magnitude].

**Variables:**
| Type | Variable | Measurement | Units |
|------|----------|-------------|-------|
| Independent | X | [How measured] | [unit] |
| Dependent | Y | [How measured] | [unit] |
| Control | Z | [Held constant] | [unit] |

**Testability Assessment:**
| Criterion | Status | Notes |
|-----------|--------|-------|
| Measurable | ✓/✗ | |
| Falsifiable | ✓/✗ | |
| Resources available | ✓/✗ | |
| Time feasible | ✓/✗ | |

**Predicted Outcome:**
If H₁ is true, we expect to observe [specific prediction].

**Test Method:**
[Brief description of how to test]

**Priority:** High/Medium/Low
**Confidence:** High/Medium/Low

---

### Hypothesis 2: [Short Title]
[Same structure...]

---

## HYPOTHESIS RELATIONSHIPS

```
H1: Primary effect
├── H2: Moderating variable
└── H3: Mediating mechanism

H4: Alternative explanation
```

---

## RECOMMENDED TESTING ORDER

1. **H1** - Foundation hypothesis, test first
   - Required resources: [list]
   - Expected duration: [time]

2. **H3** - Depends on H1 confirmation
   - Required resources: [list]

3. **H2** - Can run parallel to H3

---

## FALSIFICATION CRITERIA

### For H1 to be Rejected:
- If [specific measurable outcome]
- Statistical threshold: p > 0.05

### For H1 to be Supported:
- If [specific measurable outcome]
- Effect size: [minimum meaningful difference]

---

## ALTERNATIVE EXPLANATIONS

| Hypothesis | Alternative Explanation | How to Distinguish |
|------------|------------------------|-------------------|
| H1 | Confounding variable C | Control for C |
| H2 | Reverse causation | Time-series analysis |

---

## NEXT STEPS

1. [ ] Refine H1 with domain expert input
2. [ ] Design experiment for H1
3. [ ] Identify data sources for H2
4. [ ] Literature review for H3 support
```

## Hypothesis Generation Process

### From Observation to Hypothesis

```
Observation: "Modules near the top of the rack run hotter"

↓ Ask "Why?"

Possible Causes:
1. Heat rises (convection)
2. Reduced airflow at top
3. Higher power modules placed at top
4. Thermal coupling from modules below

↓ Formulate testable hypotheses

H1: "Vertical position affects module temperature due to convective heat accumulation"
H2: "Airflow velocity decreases with height in the rack"
H3: "Module placement strategy correlates with thermal performance"
```

### From Gap to Hypothesis

```
Literature Gap: "No studies on hot-swap thermal transients"

↓ Identify specific question

Question: "What thermal stress occurs during hot-swap?"

↓ Formulate hypothesis

H1: "Hot-swap events cause temperature spikes >20°C in adjacent modules"
H2: "Temperature stabilization after hot-swap requires >60 seconds"
```

## Common Pitfalls

### Avoid These
| Pitfall | Example | Better Version |
|---------|---------|----------------|
| Too vague | "Efficiency is important" | "Efficiency >95% reduces TCO by >10%" |
| Not falsifiable | "The system works well" | "System achieves 97% uptime" |
| Too broad | "Modularity is beneficial" | "Modularity reduces repair time by 50%" |
| Tautological | "Hot modules are warm" | "Module temperature correlates with load" |

## Integration with Other Skills

```
1. /lit-search [topic]              # Find existing knowledge
2. /gap-analysis                    # Identify research gaps
3. /hypothesis-gen                  # Generate testable hypotheses
4. /method-check                    # Design valid methodology
5. /calc-verify                     # Predict expected outcomes
```

## Example

```
/hypothesis-gen "We observed efficiency drops at partial load"
/hypothesis-gen --from-data test-results.csv
/hypothesis-gen "Literature shows no work on distributed thermal management"
```
