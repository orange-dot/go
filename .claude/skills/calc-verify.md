# Skill: calc-verify

Verify calculations and formulas for correctness. Critical for technical accuracy.

## When to Use

- After writing technical specifications
- When reviewing engineering calculations
- Before publishing performance claims
- During design reviews

## What It Checks

### 1. Formula Correctness
- Standard formulas used correctly
- No transcription errors
- Proper operator precedence

### 2. Input Validation
- Input values match specifications
- Units are consistent
- Values are in plausible ranges

### 3. Result Verification
- Recalculate all derived values
- Check order of magnitude
- Verify against reference data

### 4. Edge Cases
- Boundary conditions checked
- Division by zero avoided
- Overflow/underflow considered

### 5. Propagation of Uncertainty
- Significant figures appropriate
- Error bounds stated where relevant
- Rounding handled correctly

## Common Formulas to Verify

### Electrical
```
P = V × I                    # Power
P = I² × R = V²/R            # Power (resistive)
E = P × t                    # Energy
η = P_out / P_in × 100%      # Efficiency
```

### Thermal
```
Q = m × c × ΔT               # Heat capacity
P = (T_j - T_a) / R_th       # Thermal power
R_th = ΔT / P                # Thermal resistance
```

### Module Scaling
```
N = ceil(P_total / P_module) # Module count
P_actual = N × P_module      # Actual power
```

### Financial
```
Cost_total = N × Cost_unit   # Total cost
Cost_per_kW = Cost / P_kW    # Cost per kW
```

## Output Format

```markdown
# Calculation Verification Report

## Document: [filename]

## Summary
- Calculations checked: N
- Correct: N (%)
- Errors: N (must fix)
- Unverifiable: N (missing inputs)

## CALCULATIONS VERIFIED ✓
| Line | Formula | Inputs | Result | Status |
|------|---------|--------|--------|--------|
| 45 | P = V × I | 800V, 4.125A | 3300W ✓ | Correct |
| 67 | N = P/3.3kW | 500kW | 152 ✓ | Correct |

## ERRORS FOUND ✗ (MUST FIX)
| Line | Stated | Calculated | Error | Impact |
|------|--------|------------|-------|--------|
| 89 | 145 modules | 152 modules | 7 modules | Undercapacity |
| 102 | 95% eff | 97.5% eff | Transcription | Understated |

## INPUT MISMATCHES
| Line | Parameter | Document Value | Spec Value | Source |
|------|-----------|----------------|------------|--------|
| 34 | V_bus | 800V | 900V | SPECIFICATIONS.md |

## PLAUSIBILITY FLAGS
| Line | Calculation | Result | Flag |
|------|-------------|--------|------|
| 156 | Efficiency | 102% | >100% impossible |
| 178 | Power density | 150 kW/kg | Unusually high |

## MISSING INFORMATION
| Line | Calculation | Missing |
|------|-------------|---------|
| 45 | Thermal rise | Ambient temperature |
```

## Verification Process

1. **Extract** all calculations from document
2. **Identify** formula and inputs
3. **Recalculate** independently
4. **Compare** stated vs calculated result
5. **Flag** discrepancies with severity

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- All calculation errors must be corrected
- Results must match within significant figures
- Missing inputs must be added or referenced
- No publication with unverified critical calculations

## Plausibility Checks

| Parameter | Reasonable Range | Flag If |
|-----------|------------------|---------|
| Efficiency | 90-99% | >100% or <80% |
| Power density | 1-50 kW/kg | >100 kW/kg |
| Temperature rise | 10-80°C | >100°C |
| Cost per kW | $50-500 | <$10 or >$1000 |

## Example

```
/calc-verify tehnika/inzenjersko/en/SPECIFICATIONS.md
/calc-verify patent/03-TECHNICAL-SUPPORT/EK3-DETAILED-DESIGN.md
```
