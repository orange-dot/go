# Skill: prior-art

Search for prior art in patents and publications. Critical for patent applications.

## When to Use

- Before filing patent applications
- During invention disclosure preparation
- To assess novelty of ideas
- For freedom-to-operate analysis
- Competitive intelligence

## Search Scope

### Patent Databases
| Database | Coverage | URL |
|----------|----------|-----|
| Google Patents | Worldwide | patents.google.com |
| USPTO | US patents & applications | patft.uspto.gov |
| Espacenet | EP, WO, national | worldwide.espacenet.com |
| WIPO PatentScope | PCT applications | patentscope.wipo.int |
| Lens.org | Patents + scholarly | lens.org |

### Non-Patent Literature (NPL)
| Source | Type |
|--------|------|
| IEEE Xplore | Technical papers |
| Google Scholar | Academic papers |
| arXiv | Preprints |
| YouTube/Vimeo | Product demos |
| Trade publications | Industry news |
| Company websites | Product specs |

## Search Strategy

### 1. Claim Decomposition
Break invention into elements:
```
Invention: Modular EV charger with hot-swap
├── Element A: Modular power unit
├── Element B: Hot-swap mechanism
├── Element C: Distributed control
└── Element D: Fleet optimization
```

### 2. Keyword Development
| Element | Primary Terms | Synonyms | IPC/CPC Classes |
|---------|---------------|----------|-----------------|
| A | modular charger | scalable, building-block | H02J 7/00 |
| B | hot-swap | hot-plug, live-swap | H02J 7/02 |
| C | distributed control | decentralized, multi-agent | G05B 19/00 |
| D | fleet optimization | scheduling, load balancing | G06Q 10/00 |

### 3. Search Queries
```
# Google Patents
(modular OR scalable) AND ("electric vehicle" OR EV) AND (charger OR EVSE)

# USPTO
ACLM/(modular AND charger) AND SPEC/(electric vehicle)

# Espacenet
ti=(modular charger) AND cl=H02J7
```

## Output Format

```markdown
# Prior Art Search Report

## Invention: [brief description]
## Date: [date]
## Searcher: Claude Code

---

## EXECUTIVE SUMMARY

### Novelty Assessment
☐ **Novel** - No prior art found for key claims
☐ **Partially Novel** - Some elements have prior art
☐ **Not Novel** - Core concept disclosed in prior art

### Key Findings
- N patents found with overlapping claims
- N publications describing similar approaches
- Earliest prior art date: [date]

---

## CLAIM ELEMENT ANALYSIS

| Element | Prior Art Found | Closest Reference | Gap |
|---------|-----------------|-------------------|-----|
| A: Modular unit | Yes | US10,XXX,XXX | None |
| B: Hot-swap | Partial | EP3,XXX,XXX | Thermal management |
| C: Distributed | No | - | Novel |
| D: Fleet opt | Yes | WO2023/XXXXXX | Different approach |

---

## CLOSEST PRIOR ART

### Patents

#### 1. US10,123,456 - "Modular EV Charging System"
- **Assignee:** Company A
- **Filed:** 2020-05-15
- **Status:** Granted
- **Relevance:** HIGH
- **Overlapping Claims:**
  - Claim 1: Modular power units (matches Element A)
  - Claim 5: Scalable architecture
- **Distinguishing Features:**
  - No hot-swap capability
  - Centralized control only
- **Citation:** [1]

#### 2. EP3,456,789 - "Battery Swap Station"
- **Applicant:** Company B
- **Filed:** 2021-08-20
- **Status:** Pending
- **Relevance:** MEDIUM
- **Overlapping Claims:**
  - Hot-swap mechanism (partial match Element B)
- **Distinguishing Features:**
  - For battery swap, not charger modules
  - Different thermal approach
- **Citation:** [2]

### Publications

#### 3. IEEE Paper - "Distributed Control for EV Charging"
- **Authors:** Smith et al.
- **Published:** 2022
- **Venue:** IEEE Trans. Smart Grid
- **Relevance:** MEDIUM
- **Key Content:**
  - Multi-agent control architecture
  - No modular hardware
- **Citation:** [3]

---

## PATENT LANDSCAPE

### Key Players
| Company | Patent Count | Focus Area |
|---------|--------------|------------|
| ABB | 45 | High-power charging |
| Siemens | 38 | Grid integration |
| Tesla | 22 | Proprietary connectors |
| ChargePoint | 18 | Network/software |

### Technology Trends
- Modular architectures: Growing (2020-present)
- SiC power electronics: Accelerating
- V2G integration: Emerging

### White Space Opportunities
1. [Gap 1] - No patents on X
2. [Gap 2] - Limited coverage of Y

---

## FREEDOM TO OPERATE

### Potentially Blocking Patents
| Patent | Holder | Expiry | Risk | Mitigation |
|--------|--------|--------|------|------------|
| US9,XXX,XXX | Company C | 2035 | Medium | Design around claim 3 |

### Recommended Actions
1. Design around [specific feature]
2. Consider licensing [patent]
3. Monitor [pending application]

---

## BIBLIOGRAPHY

[1] Company A, "Modular EV Charging System," U.S. Patent 10,123,456, Jan. 15, 2022.

[2] Company B, "Battery Swap Station," European Patent Application EP3,456,789, Aug. 20, 2021.

[3] A. Smith, B. Jones, "Distributed Control for EV Charging Networks," IEEE Trans. Smart Grid, vol. 13, no. 4, pp. 2890-2901, Jul. 2022.
```

## Search Documentation

For patent applications, document:
1. **Databases searched** - List all sources
2. **Search queries** - Exact strings used
3. **Date of search** - Prior art is time-sensitive
4. **Results reviewed** - How many screened

## Integration with Patent Workflow

```
1. /prior-art "invention description"     # Find prior art
2. /claim-verify docs/invention-disclosure.md  # Verify claims
3. /paper-structure --format=patent docs/patent-draft.md  # Structure the application
4. /cite-check docs/patent-draft.md      # Verify citations
```

## Example

```
/prior-art "modular EV charger with hot-swappable power modules"
/prior-art "distributed robot coordination for battery swap"
/prior-art "V2G bidirectional charging with grid services"
```
