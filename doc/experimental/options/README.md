# Experimental Options: ROJ & MAPF-HET Inspired Go Improvements

This directory contains proposals for improving the Go language and runtime
inspired by concepts from two research papers on distributed systems and
multi-agent coordination.

## Source Papers

1. `../roj-distributed-consensus.pdf` - ROJ: Distributed Consensus and Fault
   Tolerance for Modular EV Charging Infrastructure

2. `../main.pdf` - MAPF-HET: Heterogeneous Multi-Agent Path Finding with
   Field-Centric Distributed Coordination

## Key Concepts from Papers

### From ROJ
| Concept | Description | Go Application |
|---------|-------------|----------------|
| Stigmergy | Indirect coordination through environmental signals | Scheduler, GC |
| Raft Consensus | Distributed leader election | Multi-machine runtime |
| Byzantine Fault Tolerance | Detecting/handling faulty nodes | Goroutine supervision |
| Version Vectors | Causal ordering without coordination | CRDTs, channels |
| JEZGRO Microkernel | Fault isolation, reincarnation | Process model |
| k=7 Neighbors | Scale-free topological coordination | P-to-P relationships |

### From MAPF-HET
| Concept | Description | Go Application |
|---------|-------------|----------------|
| MD-MAPF | Mixed-dimensional path finding | Dimensional classification |
| CBS (Conflict-Based Search) | Two-level conflict resolution | Scheduler conflicts |
| Potential Field Scheduling | Gradient-based resource allocation | Channel/lock scheduling |
| Slack-Field Mapping | Deadline slack → coordination signals | Urgency calculation |
| Six-Class Conflict Taxonomy | Typed conflict resolution | Conflict classification |
| Planning-Execution Bridge | Global planning with local adaptation | Hybrid scheduling |

## Options Overview

### Core Runtime Changes

| Option | Name | Effort | Risk | Impact | Status |
|--------|------|--------|------|--------|--------|
| [A](option-a-stigmergic-scheduler.md) | Stigmergic Scheduler | Medium | Low | Medium | Ready |
| [B](option-b-supervised-goroutines.md) | Supervised Goroutines | High | Medium | High | Ready |
| [C](option-c-distributed-runtime.md) | Distributed Runtime | Extreme | High | Revolutionary | Research |
| [D](option-d-crdts.md) | CRDTs in Runtime | Medium | Low | Medium | Ready |
| [E](option-e-stigmergic-gc.md) | Stigmergic GC | High | High | Medium | Research |

### New Options (from MAPF-HET)

| Option | Name | Effort | Risk | Impact | Status |
|--------|------|--------|------|--------|--------|
| [F](option-f-cbs-scheduler.md) | CBS Conflict Scheduler | High | Medium | Medium | Design |
| [G](option-g-potential-field-channels.md) | Potential Field Channels | Medium | Low | Medium | Ready |
| [H](option-h-dimensional-goroutines.md) | Dimensional Goroutines | High | Medium | High | Design |
| [I](option-i-cooperative-scheduling.md) | Cooperative Scheduling | Medium | Medium | High | Design |
| [J](option-j-correlation-metrics.md) | Correlation Metrics | Low | Low | Medium | Ready |

## Recommended Implementation Order

### Phase 1: Low-Risk Foundations
1. **Option D (CRDTs)** - Standalone package, lowest risk, proven theory
2. **Option J (Correlation Metrics)** - Observability only, enables measurement

### Phase 2: Scheduler Enhancements
3. **Option A (Stigmergic Scheduler)** - Core insight from both papers
4. **Option G (Potential Field Channels)** - Extends stigmergy to channels

### Phase 3: Advanced Scheduling
5. **Option I (Cooperative Scheduling)** - Radical change, enables predictability
6. **Option H (Dimensional Classification)** - Build on scheduler learnings
7. **Option F (CBS Conflicts)** - Principled conflict resolution

### Phase 4: Fault Tolerance
8. **Option B (Supervised Goroutines)** - Builds on earlier work

### Phase 5: GC & Research
9. **Option E (Stigmergic GC)** - High risk, requires deep scheduler knowledge
10. **Option C (Distributed Runtime)** - Long-term research project

## Synergies Between Options

```
                    ┌─────────────────────────────────────────┐
                    │          Option C: Distributed          │
                    │          (Long-term research)           │
                    └─────────────────────────────────────────┘
                                        ▲
                                        │ enables
                    ┌───────────────────┴───────────────────┐
                    │                                       │
            ┌───────┴───────┐                       ┌───────┴───────┐
            │   Option B    │                       │   Option E    │
            │  Supervised   │                       │  Stigmergic   │
            │  Goroutines   │                       │      GC       │
            └───────┬───────┘                       └───────┬───────┘
                    │                                       │
                    │ builds on                    builds on│
                    │                                       │
            ┌───────┴───────────────────────────────────────┴───────┐
            │                                                       │
            │              Options A + G + H + I + F                │
            │              (Scheduler Enhancements)                 │
            │                                                       │
            │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
            │  │    A    │  │    G    │  │    H    │  │    I    │ │
            │  │Stigmergy│◄─┤Potential│◄─┤Dimension│◄─┤Cooperat.│ │
            │  │Scheduler│  │ Channels│  │ Classif │  │Schedule │ │
            │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘ │
            │       │            │            │            │       │
            │       └────────────┴─────┬──────┴────────────┘       │
            │                          │                           │
            │                  ┌───────┴───────┐                   │
            │                  │   Option F    │                   │
            │                  │ CBS Conflicts │                   │
            │                  └───────────────┘                   │
            └───────────────────────────────────────────────────────┘
                                        │
                                        │ measured by
                                        ▼
            ┌───────────────────────────────────────────────────────┐
            │              Option J: Correlation Metrics            │
            │                  (Observability)                      │
            └───────────────────────────────────────────────────────┘
                                        │
                                        │ uses
                                        ▼
            ┌───────────────────────────────────────────────────────┐
            │                  Option D: CRDTs                      │
            │              (Standalone package)                     │
            └───────────────────────────────────────────────────────┘
```

## Shared Concepts Across Options

### Urgency/Slack Calculation
Used in: A, F, G, H, I

```go
urgency = waitTime / τ_normalize  // From MAPF-HET slack-field mapping
```

### k=7 Topological Neighbors
Used in: A, C, E, J

```go
neighbors = selectTopological(k=7)  // From starling flock research
```

### Potential Field Gradients
Used in: A, E, G

```go
Φ(v) = Φ_goal(v) - Φ_repulsive(v) + Φ_thermal(v)  // From MAPF-HET
```

### Conflict Classification
Used in: F, H

```go
type conflictClass int  // 6 classes from MAPF-HET, 10 for Go
```

### Version Vectors (CRDTs)
Used in: D, C

```go
VV[node_id] = highest_sequence_seen  // From ROJ gossip
```

## GOEXPERIMENT Flags

Each option can be enabled independently:

```bash
GOEXPERIMENT=stigmergy go build          # Option A
GOEXPERIMENT=potentialchannels go build  # Option G
GOEXPERIMENT=cbsscheduler go build       # Option F
GOEXPERIMENT=dimscheduler go build       # Option H
GOEXPERIMENT=cooperative go build        # Option I
GOEXPERIMENT=correlationmetrics go build # Option J
GOEXPERIMENT=stigmergygc go build        # Option E
```

Combine multiple:
```bash
GOEXPERIMENT=stigmergy,potentialchannels,correlationmetrics go build
```

## Verification Strategy

### Metrics to Track
| Metric | Source | Target |
|--------|--------|--------|
| Load balance convergence | MAPF-HET Table IX | O(log N) time |
| Deadline compliance | MAPF-HET Table X | 99.9% |
| Byzantine fault detection | ROJ Table III | 99.3% |
| Bandwidth overhead | MAPF-HET Section VIII-C | < 42% |
| Throughput vs centralized | MAPF-HET Table V | 97%+ |

### Benchmarks
```bash
# Scheduler benchmarks
go test -bench=BenchmarkSched runtime

# Channel benchmarks
go test -bench=BenchmarkChan runtime

# Mixed workload
go test -bench=. -benchtime=10s runtime
```

## Related Go Issues & Proposals

- golang/go#18802 - NUMA-aware scheduler
- golang/go#14592 - Better goroutine lifecycle management
- golang/go#17969 - Concurrent map improvements
- golang/go#48287 - GC pacer improvements
- golang/go#24543 - Goroutine local storage

## Academic Skills (Preserved)

The following academic/research skills from the original setup are preserved:
- `claim-verify.md` - Audit technical claims
- `peer-review.md` - Simulate academic peer review
- `lit-search.md` - Literature search
- `hypothesis-gen.md` - Generate hypotheses
- `gap-analysis.md` - Identify research gaps
- And others in the `skills/` directory

## References

### Papers
1. ROJ: Distributed Consensus (Janjatović, 2026)
2. MAPF-HET: Heterogeneous Multi-Agent Path Finding (Janjatović, 2026)
3. Cavagna et al., "Scale-free correlations in starling flocks" (PNAS 2010)
4. Sharon et al., "Conflict-based search for optimal MAPF" (AI 2015)
5. Ongaro & Ousterhout, "In Search of an Understandable Consensus Algorithm" (2014)

### Go Documentation
- golang.org/s/go11sched - Go scheduler design
- tip.golang.org/doc/gc-guide - GC guide
- go.dev/doc/contribute - Contribution guidelines
