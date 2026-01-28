# Skill: reproduce-check

Audit work for reproducibility. Essential for code, data, and scientific claims.

## When to Use

- Before publishing research
- When sharing code or analysis
- For open-source projects
- During peer review preparation

## Reproducibility Levels

| Level | Description | Requirements |
|-------|-------------|--------------|
| **R4** | Full reproduction | Code, data, environment, one-click run |
| **R3** | Partial reproduction | Code, data, manual environment setup |
| **R2** | Method reproduction | Detailed steps, data description |
| **R1** | Conceptual only | High-level description only |

**Target: R3 minimum, R4 for computational work**

## What It Checks

### 1. Code Availability
- [ ] Source code provided
- [ ] License specified
- [ ] Version controlled
- [ ] README with instructions

### 2. Dependencies
- [ ] All dependencies listed
- [ ] Version numbers specified
- [ ] Installation instructions
- [ ] Known compatibility issues

### 3. Data
- [ ] Data available or described
- [ ] Data format documented
- [ ] Preprocessing steps documented
- [ ] Data license/restrictions

### 4. Environment
- [ ] OS requirements stated
- [ ] Hardware requirements stated
- [ ] Software versions specified
- [ ] Container/environment file provided

### 5. Steps
- [ ] Complete step-by-step instructions
- [ ] Expected outputs described
- [ ] Error handling documented
- [ ] Verification procedure provided

## Output Format

```markdown
# Reproducibility Audit Report

## Document/Project: [name]

## Summary
- Reproducibility Level: RX
- Blockers: N (must fix)
- Warnings: N (should fix)

## REPRODUCIBILITY CHECKLIST

### Code ✓/✗
| Item | Status | Notes |
|------|--------|-------|
| Source available | ✓/✗ | Location |
| License | ✓/✗ | MIT/Apache/etc |
| Version control | ✓/✗ | Git/other |
| README | ✓/✗ | Complete/partial |

### Dependencies ✓/✗
| Item | Status | Notes |
|------|--------|-------|
| Listed | ✓/✗ | requirements.txt/package.json |
| Versions pinned | ✓/✗ | Exact versions |
| Install instructions | ✓/✗ | Clear/unclear |

### Data ✓/✗
| Item | Status | Notes |
|------|--------|-------|
| Available | ✓/✗ | Location/URL |
| Format documented | ✓/✗ | CSV/JSON/etc |
| Preprocessing | ✓/✗ | Steps documented |
| Size | - | X MB/GB |

### Environment ✓/✗
| Item | Status | Notes |
|------|--------|-------|
| OS | ✓/✗ | Windows/Linux/macOS |
| Hardware | ✓/✗ | CPU/GPU requirements |
| Container | ✓/✗ | Dockerfile/environment.yml |

### Steps ✓/✗
| Item | Status | Notes |
|------|--------|-------|
| Instructions | ✓/✗ | Complete/partial |
| Expected output | ✓/✗ | Described/missing |
| Verification | ✓/✗ | Test procedure |

## BLOCKERS (MUST FIX)
1. No dependency versions specified
2. Data preprocessing steps missing
3. Expected output not described

## WARNINGS (SHOULD FIX)
1. No containerized environment
2. Hardware requirements vague

## MISSING FILES
| Expected | Purpose | Priority |
|----------|---------|----------|
| requirements.txt | Python dependencies | Critical |
| Dockerfile | Environment | High |
| data/README.md | Data documentation | High |

## REPRODUCTION STEPS (Inferred)
1. Clone repository
2. Install dependencies: `pip install -r requirements.txt`
3. Download data: [URL or instructions]
4. Run: `python main.py`
5. Expected output: [description]
```

## Reproducibility Requirements by Type

### Software Projects
```
project/
├── README.md          # Overview, quick start
├── LICENSE            # Open source license
├── requirements.txt   # or package.json, Cargo.toml
├── Dockerfile         # or environment.yml
├── src/               # Source code
├── tests/             # Test suite
└── docs/              # Documentation
```

### Research Projects
```
research/
├── README.md          # Overview, reproduction steps
├── paper.pdf          # Published paper
├── code/              # Analysis code
├── data/              # or data/README.md with instructions
├── figures/           # Generated figures
├── environment.yml    # Conda environment
└── results/           # Expected outputs
```

### Hardware Projects
```
hardware/
├── README.md          # Overview
├── BOM.csv            # Bill of materials
├── schematics/        # Circuit diagrams
├── firmware/          # Embedded code
├── test/              # Test procedures
└── manufacturing/     # Production files
```

## STRICT Mode Rules

This skill operates in **STRICT** mode:
- R3 minimum required for publication
- All dependencies must have versions
- Data must be available or fully described
- Steps must be complete and tested

## Example

```
/reproduce-check web/
/reproduce-check research/simulation-study/
/reproduce-check firmware/ek3-controller/
```
