# EK-KOR2 Complexity Management (Dev vs Prod)

This document defines how we keep the codebase understandable during
development and how we ship a minimal, stable production package set.

## Goals
- Keep dev work fast without contaminating the production runtime.
- Make "what is stable" obvious.
- Ship only essential modules in production.
- Prevent experimental features from creeping into release builds.

## Principles
- **Single responsibility**: each module has one job and a clear API.
- **Explicit boundaries**: production packages cannot depend on tests, sims, or tools.
- **Feature gating**: optional features must be behind build flags.
- **Determinism**: production builds must be repeatable and minimal.
- **Evidence over claims**: anything in prod has test vectors or hardware evidence.

## Development Profile (full surface)
Used for iteration, exploration, and validation.

Included:
- All core modules + HALs.
- Examples, test harness, and benchmarks.
- Simulators and fault injectors.
- Design docs and research notes.

Relevant paths (today):
- `c/src/*` (core modules)
- `c/src/hal/*` (all HALs)
- `c/examples/*`
- `c/test/*`
- `c/sim/*` (simulation harnesses)
- `docs/*`, `tools/*`, `renode/*`

## Production Profile (stripped surface)
Used for release builds and validation on target hardware.

Included (only what is needed to run):
- **Core runtime**
  - `ekk_types`, `ekk_field`, `ekk_topology`
- **Coordination**
  - `ekk_consensus` (threshold + quarantine)
  - `ekk_raft` (leader election)
  - `ekk_partition` (split‑brain prevention)
  - `ekk_auth` (Chaskey MAC)
- **Integration**
  - `ekk_module` (module runtime glue)
- **HAL**
  - exactly one target HAL (e.g., `ekk_hal_stm32g474.c`)

Excluded in production:
- Tests, benchmarks, sims, examples.
- Multi‑platform HALs not used in release.
- Experimental modules and research docs.

## Proposed Production Package Set
The production build should expose only these packages and headers:

1) `ekk-core`
   - Types + fixed‑point utilities
   - Field definitions and decay

2) `ekk-topology`
   - k‑neighbor selection

3) `ekk-consensus`
   - Threshold voting + quarantine protocol

4) `ekk-raft`
   - Leader election only

5) `ekk-partition`
   - Partition detection, freeze mode, reconciliation

6) `ekk-auth`
   - Chaskey MAC compute/verify

7) `ekk-module`
   - Integration of the above

8) `ekk-hal-<platform>`
   - Single platform HAL for the production target

Optional (only if used in prod):
- `ekk-gossip` (event gossip + version vectors)

## Build Profiles (CMake)
Baseline flags for production:
- `-DEKK_PLATFORM=<target>`
- `-DEKK_BUILD_TESTS=OFF`
- `-DEKK_BUILD_EXAMPLES=OFF`
- `-DEKK_BUILD_DOCS=OFF`

Recommended build behaviors for production:
- No debug output by default (or compile‑time switch).
- No unused HALs included.
- No experimental modules compiled in.

## Promotion Path (Dev -> Prod)
A feature should only move to production when:
1) API is documented and stable.
2) Test vectors cover the core logic.
3) Simulation results are recorded.
4) Hardware/bench evidence exists for timing‑critical behavior.

## Release Checklist (Production)
- [ ] Target HAL only (single platform).
- [ ] Tests/examples/sims excluded.
- [ ] All external interfaces documented.
- [ ] Evidence artifacts linked (logs, timings, test vectors).
- [ ] Versioned build outputs archived.

## Notes
This is intentionally conservative. The goal is to make releases easy to
reason about even as the research surface grows.
