# Skill: units-check

Verify SI unit consistency throughout documents. Quick check that catches common errors.

## When to Use

- After writing technical specifications
- When combining content from multiple sources
- Before publishing any technical document
- During calculation verification

## SI Unit Rules

### Base Units
| Quantity | Unit | Symbol |
|----------|------|--------|
| Length | metre | m |
| Mass | kilogram | kg |
| Time | second | s |
| Current | ampere | A |
| Temperature | kelvin | K |
| Amount | mole | mol |
| Luminosity | candela | cd |

### Derived Units (Common)
| Quantity | Unit | Symbol | Base |
|----------|------|--------|------|
| Force | newton | N | kg·m/s² |
| Energy | joule | J | N·m |
| Power | watt | W | J/s |
| Voltage | volt | V | W/A |
| Resistance | ohm | Ω | V/A |
| Capacitance | farad | F | C/V |
| Inductance | henry | H | Wb/A |
| Frequency | hertz | Hz | 1/s |

### Prefix Rules
| Prefix | Symbol | Factor |
|--------|--------|--------|
| giga | G | 10⁹ |
| mega | M | 10⁶ |
| kilo | k | 10³ |
| milli | m | 10⁻³ |
| micro | μ | 10⁻⁶ |
| nano | n | 10⁻⁹ |

**STRICT: Lowercase k for kilo (kW, not KW)**

## What It Checks

### 1. Capitalization Errors
- ❌ KW, Kw → ✓ kW
- ❌ KWH, kwh → ✓ kWh
- ❌ Mhz → ✓ MHz
- ❌ ma → ✓ mA

### 2. Unit Consistency
- Same quantity uses same unit throughout
- No mixing imperial and SI
- Consistent prefix usage

### 3. Dimensional Analysis
- Equations balance dimensionally
- Conversion factors correct
- No unit mismatches (W = V × A, not V + A)

### 4. Temperature
- Kelvin for absolute, Celsius for practical
- °C requires degree symbol
- Temperature differences in K or °C (not both)

### 5. Non-SI Units (Flag for Review)
- Acceptable: °C, L (litre), h (hour), min
- Convert: inches, feet, pounds, BTU, HP

## Output Format

```markdown
# Units Verification Report

## Document: [filename]

## Summary
- Units checked: N
- Correct: N (%)
- Errors: N (must fix)
- Warnings: N (review)

## ERRORS (MUST FIX)
| Line | Found | Correct | Context |
|------|-------|---------|---------|
| 45 | "150 KW" | "150 kW" | Capitalization |
| 78 | "V + A = W" | "V × A = W" | Dimensional error |

## WARNINGS (Review)
| Line | Found | Note |
|------|-------|------|
| 23 | "2 inches" | Convert to mm |

## Dimensional Analysis
| Equation | Line | Status |
|----------|------|--------|
| P = V × I | 56 | ✓ W = V × A |
| E = P × t | 89 | ✓ J = W × s |

## Consistency Check
| Quantity | Units Used | Recommendation |
|----------|------------|----------------|
| Power | kW, W | Standardize to kW for >1000W |
```

## Common Error Patterns

```
# Capitalization
KW → kW
KWH → kWh
Kwh → kWh
MHZ → MHz
mhz → MHz

# Spacing
150kW → 150 kW (space between number and unit)
3.3 kW → 3.3 kW ✓

# Symbols
ohm → Ω
micro → μ (not u)
```

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- All unit errors must be corrected
- No publication with dimensional errors
- Non-SI units must be converted or justified

## Example

```
/units-check tehnika/inzenjersko/en/SPECIFICATIONS.md
/units-check patent/03-TECHNICAL-SUPPORT/EK3-DETAILED-DESIGN.md
```
