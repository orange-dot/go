# Skill: lit-search

Search for relevant literature on a topic. Foundation for research work.

## When to Use

- Starting a new research area
- Writing literature review sections
- Finding prior art for patents
- Validating novelty of ideas
- Finding citations for claims

## Search Strategy

### 1. Query Formulation
- Identify key concepts
- Find synonyms and related terms
- Combine with Boolean operators
- Consider field-specific terminology

### 2. Source Hierarchy

| Priority | Source Type | Examples |
|----------|-------------|----------|
| 1 | Peer-reviewed journals | IEEE, ACM, Elsevier, Springer |
| 2 | Conference proceedings | ICRA, APEC, ECCE, DATE |
| 3 | Standards | ISO, IEC, IEEE, SAE |
| 4 | Technical reports | NREL, DOE, national labs |
| 5 | Theses/dissertations | ProQuest, university repos |
| 6 | Preprints | arXiv, SSRN |
| 7 | Patents | USPTO, EPO, WIPO |
| 8 | Industry white papers | Use with caution |

### 3. Databases to Search

**Engineering/Technical:**
- IEEE Xplore
- ACM Digital Library
- ScienceDirect
- Springer Link
- Google Scholar

**Patents:**
- Google Patents
- USPTO PatFT/AppFT
- Espacenet (EPO)
- WIPO PatentScope

**Standards:**
- ISO Online Browsing Platform
- IEC Webstore
- IEEE Standards

## Output Format

```markdown
# Literature Search Report

## Topic: [search topic]
## Date: [date]
## Queries Used:
- "[query 1]"
- "[query 2]"

---

## SUMMARY
- Total sources found: N
- Highly relevant: N
- Moderately relevant: N
- Background: N

## KEY FINDINGS

### Seminal Works (Must Cite)
| Ref | Authors | Title | Year | Venue | Relevance |
|-----|---------|-------|------|-------|-----------|
| [1] | Smith et al. | "Title" | 2023 | IEEE Trans. | Foundational theory |

### Recent Advances (Last 3 Years)
| Ref | Authors | Title | Year | Venue | Contribution |
|-----|---------|-------|------|-------|--------------|
| [2] | Jones | "Title" | 2024 | Conf. | Novel method |

### Related Work
| Ref | Authors | Title | Year | Relevance |
|-----|---------|-------|------|-----------|
| [3] | Lee | "Title" | 2022 | Similar approach |

### Standards & Specifications
| Ref | Standard | Title | Status |
|-----|----------|-------|--------|
| [4] | ISO 15118 | V2G Communication | Active |

---

## RESEARCH GAPS IDENTIFIED
1. [Gap 1] - No work addresses X
2. [Gap 2] - Limited studies on Y

## CONFLICTING FINDINGS
1. [Topic] - Author A claims X, Author B claims Y

## RECOMMENDED READING ORDER
1. Start with [1] for foundation
2. Then [2] for current state
3. Review [4] for standards context

---

## BIBLIOGRAPHY (IEEE Format)

[1] A. Smith, B. Jones, and C. Lee, "Article title here," IEEE Trans. Power Electron., vol. 38, no. 5, pp. 1234-1245, May 2023.

[2] D. Jones, "Conference paper title," in Proc. IEEE APEC, Orlando, FL, USA, 2024, pp. 567-572.

[3] ...
```

## Search Tactics

### Snowball Method
1. Find one highly relevant paper
2. Check its references (backward)
3. Check who cited it (forward)
4. Repeat for new relevant papers

### Keyword Evolution
```
Initial: "modular EV charger"
Expanded: "modular" OR "scalable" AND "electric vehicle" AND "charging"
Refined: "modular DC fast charging" OR "scalable EVSE"
```

### Filter Criteria
- Date range (e.g., last 5 years for state-of-art)
- Publication type (journal vs conference)
- Citation count (seminal works)
- Open access availability

## Quality Assessment

| Criterion | Check |
|-----------|-------|
| Peer review | Published in indexed journal/conference? |
| Citations | How many times cited? |
| Methodology | Methods clearly described? |
| Data | Results reproducible? |
| Conflicts | Funding/affiliation bias? |

## Integration with Other Skills

- Feed results to `/cite-check` for proper formatting
- Use findings in `/claim-verify` as evidence sources
- Input to `/gap-analysis` for research opportunities
- Support `/prior-art` for patent searches

## Example

```
/lit-search "modular EV charging architecture"
/lit-search "V2G bidirectional power electronics"
/lit-search "distributed battery swap systems"
```
