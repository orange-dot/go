# Skill: method-check

Validate experimental and analytical methodology. Essential for research credibility.

## When to Use

- Before publishing experimental results
- When reviewing research methodology
- During design of experiments
- For simulation validation

## What It Checks

### 1. Objective Clarity
- Clear research question/hypothesis
- Measurable success criteria
- Scope well-defined

### 2. Method-Objective Alignment
- Chosen method appropriate for objective
- Alternative methods considered
- Method limitations acknowledged

### 3. Variables
- Independent variables identified
- Dependent variables measured
- Control variables held constant
- Confounding variables addressed

### 4. Measurement
- Instruments specified
- Calibration documented
- Uncertainty quantified
- Sample size justified

### 5. Bias Identification
- Selection bias
- Measurement bias
- Confirmation bias
- Publication bias

### 6. Statistical Validity
- Appropriate statistical tests
- Significance levels stated
- Effect sizes reported
- Confidence intervals given

## Methodology Checklist

### Experimental Studies
- [ ] Hypothesis stated
- [ ] Control group defined
- [ ] Randomization used
- [ ] Blinding applied (if applicable)
- [ ] Sample size calculated
- [ ] Measurement protocol documented
- [ ] Error analysis performed

### Simulation Studies
- [ ] Model assumptions stated
- [ ] Boundary conditions defined
- [ ] Mesh independence verified
- [ ] Time step independence verified
- [ ] Validation against reference data
- [ ] Sensitivity analysis performed

### Analytical Studies
- [ ] Assumptions explicitly stated
- [ ] Derivation steps shown
- [ ] Limiting cases verified
- [ ] Numerical verification

## Output Format

```markdown
# Methodology Verification Report

## Document: [filename]

## Summary
- Methodology score: X/10
- Critical gaps: N
- Recommendations: N

## OBJECTIVE ANALYSIS
| Aspect | Status | Notes |
|--------|--------|-------|
| Research question | ✓/✗ | Clear/Unclear |
| Success criteria | ✓/✗ | Defined/Missing |
| Scope | ✓/✗ | Well-bounded/Unbounded |

## VARIABLES IDENTIFIED
| Type | Variable | Documented | Notes |
|------|----------|------------|-------|
| Independent | Temperature | ✓ | 25-85°C |
| Dependent | Efficiency | ✓ | Measured |
| Control | Voltage | ✗ | Not specified |
| Confounding | Humidity | ✗ | Not addressed |

## METHOD APPROPRIATENESS
| Objective | Method Used | Appropriate? | Alternative |
|-----------|-------------|--------------|-------------|
| Efficiency measurement | Calorimetric | ✓ | Electrical input-output |
| Thermal model | FEA simulation | ✓ | Analytical, experimental |

## BIAS ASSESSMENT
| Bias Type | Risk Level | Mitigation |
|-----------|------------|------------|
| Selection | Low | Random sampling |
| Measurement | Medium | Calibration documented |
| Confirmation | High | No blinding |

## LIMITATIONS
| Limitation | Stated? | Severity |
|------------|---------|----------|
| Temperature range | ✓ | Low |
| Sample size | ✗ | High |
| Model assumptions | ✓ | Medium |

## GAPS IDENTIFIED (MUST ADDRESS)
1. Control variables not specified
2. Measurement uncertainty not quantified
3. No sensitivity analysis

## RECOMMENDATIONS
1. Add uncertainty analysis (±X%)
2. Specify control conditions
3. Discuss limitations section
```

## Method Quality Levels

| Level | Score | Criteria |
|-------|-------|----------|
| Rigorous | 9-10 | All criteria met, peer-review ready |
| Sound | 7-8 | Minor gaps, acceptable for internal use |
| Adequate | 5-6 | Significant gaps, needs improvement |
| Weak | 3-4 | Major methodological issues |
| Insufficient | 1-2 | Fundamental problems, not credible |

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- All methodological gaps must be addressed
- Limitations must be explicitly stated
- Uncertainty must be quantified
- No publication with score <7

## Common Methodological Errors

1. **Undocumented assumptions** - State all assumptions
2. **Missing control conditions** - Specify what was held constant
3. **No error analysis** - Quantify uncertainty
4. **Overgeneralization** - Stay within validated range
5. **Single data point** - Require multiple measurements

## Example

```
/method-check docs/thermal-test-report.md
/method-check docs/simulation-results.md
```
