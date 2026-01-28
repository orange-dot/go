# MAPF-HET Repo Status (2026-01-26)

Scope: mapf-het-research subtree only.

## Summary
- Planning code is real and fairly complete (2D/3D A*, CBS variants, energy and deadline constraints, mixed-dimensional conflict handling, potential fields, MCTS).
- Tests exist but are small and mostly unit-level; no benchmark datasets or integration runs are stored in the repo.
- Paper and docs claim quantitative results, but there are no run scripts, datasets, logs, or CSVs to reproduce the numbers.
- README and older review docs reference folders and tooling that are not present; a few implementation claims in the paper are not traceable in code.

## What exists (code inventory)
- Algorithms: `internal/algo/` (CBS, Mixed-CBS, Energy-CBS, Deadline-CBS, Hybrid-CBS, Stochastic ECBS, MCTS, Prioritized, A* 2D/3D, potential fields).
- Core models: `internal/core/` (robots, tasks, workspace graph, airspace layers, instances, solutions).
- Bridge: `internal/bridge/field_bridge.go` (planning to execution field mapping).
- Visualization: `internal/vis/` + `cmd/mapfhetvis/` (Gio UI) and `mapfhetvis.exe`.
- CLI: `cmd/mapfhet/` and `mapfhet.exe`.
- ROJ consensus code: `ek-roj/` (Rust core + node variants).
- Integration samples: `api-integration-samples/` (not wired to core packages).
- Paper and docs: `paper/` (TeX + PDF), `docs/ALGORITHMIC-CONTRIBUTIONS.md`, `docs/INTEGRATION-MAP.md`.

## Tests (present)
- `internal/algo/solver_test.go`: conflict detection, solver sanity on toy grid.
- `internal/algo/fixes_test.go`: A* timing, edge constraints, multi-goal, schedule population, hover energy.
- `internal/core/types_test.go`: task/robot compatibility.

No benchmark harness, no dataset fixtures, no integration or HIL tests in this subtree.

## Doc and code mismatches
- `mapf-het-research/README.md` references `internal/sim/` and `testdata/`, but those directories do not exist.
- `docs/CODE-REVIEW-2026-01-21.md` reports coverage and test failures that are not reproducible from the current tree (no coverage tooling or testdata in repo).
- `paper/sections/07-implementation.tex` claims Fibonacci heap and spatial hashing; code uses Go `container/heap` (binary heap) and no spatial hash is obvious in code.
- Paper mentions compressed waypoint format; code uses full path arrays with small smoothing/visual skips, but no dedicated compression layer exists.

## Claims vs evidence (current state)
| Claim (paper) | Location | Evidence in repo | Status |
|---|---|---|---|
| 5.6x speedup over baseline CBS | `paper/sections/08-evaluation.tex`, `paper/sections/09-conclusion.tex` | No datasets or benchmark runs | Unverified |
| 99% success at 10% charging station density | `paper/sections/08-evaluation.tex` | No datasets or benchmark runs | Unverified |
| P99 latency under 1ms at 2048 modules | `paper/sections/08-evaluation.tex` | No latency logs or traces | Unverified |
| 42.6% bandwidth utilization with k=7 | `paper/sections/08-evaluation.tex`, `paper/sections/07-implementation.tex` | No traffic model or measurements | Unverified |
| 0.1% deadline violations and zero energy violations over 24h | `paper/sections/08-evaluation.tex`, `paper/sections/09-conclusion.tex` | No run logs or CSVs | Unverified |
| O(log N) convergence with k=7 and 520ms at 2048 modules | `paper/sections/06-theory.tex`, `paper/sections/09-conclusion.tex` | No convergence traces | Unverified |

## Evidence inventory (repo-wide)
- `evidence/` contains emulator boot logs only; no MAPF-HET artifacts or benchmark outputs.

## Recommended next steps
- Add reproducible benchmarks, datasets, and result artifacts; see `mapf-het-research/docs/REPRODUCIBILITY-PLAN.md`.
- Align README and CODE-REVIEW docs with the current tree (either add missing folders or update docs).
- Wire integration samples to real planner outputs or move them to an explicit "concept only" area.
