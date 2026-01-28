# Skill: term-check

Verify terminology consistency throughout documents. Prevents confusion and improves clarity.

## When to Use

- Multi-author documents
- Long technical documents
- After merging content from multiple sources
- Before publication
- During translation preparation

## What It Checks

### 1. Term Consistency
Same concept uses same term throughout:
- "module" vs "unit" vs "block"
- "charger" vs "EVSE" vs "charging station"
- "controller" vs "MCU" vs "processor"

### 2. Acronym Usage
- Defined on first use
- Used consistently after definition
- No conflicting definitions
- Spelled out in abstracts

### 3. Capitalization
- Product names consistent
- Technical terms consistent
- Headings follow style guide

### 4. Spelling Variants
- British vs American English
- Technical term variants
- Company/product names

### 5. Terminology Evolution
- Deprecated terms flagged
- Preferred terms suggested
- Industry standards followed

## Output Format

```markdown
# Terminology Consistency Report

## Document: [filename]
## Date: [date]

---

## SUMMARY

| Category | Issues | Severity |
|----------|--------|----------|
| Term variants | N | 🔴/🟡/🟢 |
| Acronyms | N | 🔴/🟡/🟢 |
| Capitalization | N | 🔴/🟡/🟢 |
| Spelling | N | 🔴/🟡/🟢 |

---

## TERM VARIANTS (Must Standardize)

### Concept: Power Conversion Module

| Variant | Count | Lines | Recommendation |
|---------|-------|-------|----------------|
| "module" | 45 | 12, 34, 56... | ✓ PREFERRED |
| "unit" | 12 | 78, 90... | → "module" |
| "block" | 3 | 102, 115... | → "module" |

**Action:** Replace all "unit" and "block" with "module" when referring to EK3.

---

### Concept: Electric Vehicle Supply Equipment

| Variant | Count | Lines | Recommendation |
|---------|-------|-------|----------------|
| "EVSE" | 23 | ... | ✓ PREFERRED (after definition) |
| "charger" | 18 | ... | OK for informal |
| "charging station" | 5 | ... | → "EVSE" or "charger" |
| "charging point" | 2 | ... | → "EVSE" |

---

## ACRONYM AUDIT

### Properly Defined ✓
| Acronym | Definition | First Use |
|---------|------------|-----------|
| EV | Electric Vehicle | Line 5 |
| SiC | Silicon Carbide | Line 23 |
| CAN | Controller Area Network | Line 45 |

### Issues ✗

#### Undefined Acronyms
| Acronym | Lines Used | Suggested Definition |
|---------|------------|---------------------|
| LLC | 34, 67, 89 | LLC (resonant converter topology) |
| DAB | 56, 78 | DAB (Dual Active Bridge) |

#### Conflicting Definitions
| Acronym | Definition 1 | Definition 2 | Resolution |
|---------|--------------|--------------|------------|
| PM | "Power Module" (L23) | "Phase Margin" (L156) | Use different term for one |

#### Used Before Definition
| Acronym | First Use | Definition | Action |
|---------|-----------|------------|--------|
| V2G | Line 12 | Line 89 | Move definition to L12 |

---

## CAPITALIZATION ISSUES

| Term | Variants Found | Standard |
|------|----------------|----------|
| Silicon Carbide | "silicon carbide", "Silicon carbide" | "silicon carbide" (common noun) |
| CAN-FD | "CAN-FD", "CAN FD", "CanFD" | "CAN FD" (standard) |
| Elektrokombinacija | "elektrokombinacija" | "Elektrokombinacija" (proper noun) |

---

## SPELLING CONSISTENCY

### British vs American
| British | American | Count | Recommendation |
|---------|----------|-------|----------------|
| colour | color | 3/12 | Standardize to American |
| optimise | optimize | 5/8 | Standardize to American |
| centre | center | 2/15 | Standardize to American |

### Technical Variants
| Variant A | Variant B | Standard |
|-----------|-----------|----------|
| hot-swap | hotswap | "hot-swap" (hyphenated) |
| realtime | real-time | "real-time" (hyphenated) |
| bi-directional | bidirectional | "bidirectional" (one word) |

---

## TERMINOLOGY GLOSSARY

Based on document analysis, recommended standard terms:

| Concept | Standard Term | Avoid |
|---------|---------------|-------|
| 3.3 kW unit | EK3 module | unit, block, device |
| Multiple modules | array, system | bank, cluster |
| Add modules | scale up | expand, grow |
| Remove modules | scale down | reduce, shrink |
| Replace while running | hot-swap | hot-plug, live-swap |
| Grid to vehicle | charging | G2V |
| Vehicle to grid | V2G, discharging | reverse flow |

---

## RECOMMENDED ACTIONS

### Immediate (Search & Replace)
1. "unit" → "module" (12 occurrences)
2. "charging station" → "EVSE" (5 occurrences)

### Manual Review
1. Line 156: "PM" - clarify which meaning
2. Line 203: Check if "charger" or "EVSE" appropriate

### Add Definitions
1. Line 34: Add "LLC (resonant converter topology)"
2. Line 56: Add "DAB (Dual Active Bridge)"
```

## Domain-Specific Terminology

### EV Charging
| Preferred | Avoid | Reason |
|-----------|-------|--------|
| EVSE | charger (informal OK) | Standard term |
| charging session | charge event | Industry standard |
| connector | plug | Technical accuracy |
| CCS | CCS2, Combo2 | Be specific about variant |

### Power Electronics
| Preferred | Avoid | Reason |
|-----------|-------|--------|
| topology | architecture | Specific meaning |
| switching frequency | switching speed | Precise term |
| efficiency | yield | Avoid confusion |
| power density | specific power | SI consistency |

### Software/Firmware
| Preferred | Avoid | Reason |
|-----------|-------|--------|
| firmware | embedded software | Specificity |
| CAN FD | CAN-FD, CANFD | Standard spacing |
| real-time | realtime | Standard hyphenation |

## Integration with Other Skills

```
1. /term-check document.md           # Find inconsistencies
2. /rigor-fix document.md            # Auto-fix simple cases
3. /translate-sr document.md         # Translation needs consistent source
```

## Example

```
/term-check tehnika/inzenjersko/en/SPECIFICATIONS.md
/term-check patent/01-IP-FOUNDATION/
/term-check --glossary project-glossary.md document.md
```
