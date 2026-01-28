# MAPF-HET Reproducibility Plan (2026-01-26)

Goal: make paper metrics reproducible from a clean repo checkout with deterministic runs and stored artifacts.

## 1) Artifact layout (proposed)

```
mapf-het-research/
  benchmarks/
    instances/           # JSON instances (seeded)
    configs/             # Solver configs (YAML/JSON)
    baselines/           # Baseline definitions
    results/             # CSV outputs per run
    plots/               # Generated plots (optional)
  tools/
    gen_instances.go     # Deterministic instance generator
    run_benchmarks.go    # Batch runner for planners
    summarize.py         # Aggregations + tables
    export_tex.py        # Export tables for paper
    verify_claims.py     # Check claim thresholds

evidence/
  mapf-het/
    YYYY-MM-DD/
      run-metadata.json
      results.csv
      logs/
      checksums.txt
```

## 2) Instance schema (minimal)

```
{
  "seed": 12345,
  "workspace": { "vertices": [...], "edges": [...] },
  "agents": [{ "id": 0, "type": "A|B|C", "start": 12, "battery_wh": 100 }],
  "tasks": [{ "id": 0, "type": "SwapModule", "vertex": 77, "deadline_s": 120, "duration_s": 20 }],
  "charging_pads": [5, 19, 42],
  "time_horizon_s": 600
}
```

## 3) Reproduction scripts (minimum viable)

- `tools/gen_instances.go`
  - Inputs: seed, counts, densities (agents, tasks, pads).
  - Outputs: deterministic instance JSON set.

- `tools/run_benchmarks.go`
  - Inputs: instance list + solver config.
  - Outputs: `results.csv` with runtime, success, makespan, energy violations, deadline violations.

- `tools/summarize.py`
  - Inputs: one or more result CSVs.
  - Outputs: aggregated tables, percentiles, speedups.

- `tools/export_tex.py`
  - Inputs: aggregated tables.
  - Outputs: `paper/sections/08-evaluation.generated.tex` (include from main).

- `tools/verify_claims.py`
  - Inputs: aggregated tables + thresholds (from paper).
  - Outputs: pass/fail report for each claim.

## 4) Claim-to-artifact mapping

| Claim | Required artifact | How to compute |
|---|---|---|
| 5.6x speedup vs CBS | `results.csv` for CBS vs Mixed-CBS on same instance set | median runtime ratio (and CI) |
| 99% success at 10% pads | `results.csv` for E-CBS on 10% density instances | success_rate >= 0.99 |
| P99 latency under 1ms | latency trace or sim output | compute P99 from trace |
| 42.6% bandwidth with k=7 | traffic model output | aggregate kbps and utilization |
| 0.1% deadline violation + zero energy violations (24h) | long-run simulation log | compute rates from log |
| O(log N) convergence and 520ms at 2048 | convergence trace | time-to-95% load balance |

## 5) Run protocol (for each published artifact)

1. Record commit hash, OS, Go version, and tool versions in `run-metadata.json`.
2. Generate instances with fixed seeds.
3. Run planners and store `results.csv`.
4. Produce aggregated tables and claim checks.
5. Copy outputs to `evidence/mapf-het/YYYY-MM-DD/` and link from docs/paper.

## 6) Immediate low-effort path

- Start with planning benchmarks only (CBS vs Mixed-CBS vs Deadline-CBS).
- Publish the instance generator and results CSVs.
- Defer execution-layer claims (latency/bandwidth) until ROJ or simulator traces exist.
