# Skill: runtime-trace

Trace and debug Go runtime behavior (scheduler, GC, memory).

## When to Use

- Debugging scheduler issues
- Investigating GC pauses
- Analyzing memory allocation patterns
- Understanding goroutine behavior
- Profiling runtime performance

## GODEBUG Environment Variables

### Garbage Collector
```bash
GODEBUG=gctrace=1 ./program           # GC timing and stats
GODEBUG=gcpacertrace=1 ./program      # GC pacer decisions
GODEBUG=gccheckmark=1 ./program       # verify GC marking (slow)
```

### Scheduler
```bash
GODEBUG=schedtrace=1000 ./program     # scheduler stats every 1000ms
GODEBUG=scheddetail=1 ./program       # detailed scheduler info
```

### Memory
```bash
GODEBUG=scavtrace=1 ./program         # trace scavenger activity
GODEBUG=memprofilerate=1 ./program    # high-resolution heap profiling (very verbose)
GODEBUG=inittrace=1 ./program         # package init timing
```

### Debugging
```bash
GODEBUG=asyncpreemptoff=1 ./program   # disable async preemption
GODEBUG=cgocheck=1 ./program          # cgo pointer checks (default)
```

For stricter cgo checking, build with `GOEXPERIMENT=cgocheck2`.

## GC Trace Output Format

```
gc 1 @0.012s 2%: 0.026+0.44+0.003 ms clock, 0.21+0.088/0.40/0+0.031 ms cpu, 4->4->0 MB, 5 MB goal, 8 P
```

| Field | Meaning |
|-------|---------|
| `gc 1` | GC cycle number |
| `@0.012s` | Time since program start |
| `2%` | % of time spent in GC |
| `0.026+0.44+0.003 ms clock` | STW sweep, concurrent mark, STW mark termination |
| `4->4->0 MB` | Heap before, heap after, live data |
| `5 MB goal` | Target heap size |
| `8 P` | Number of processors |

## Scheduler Trace Output

```
SCHED 1000ms: gomaxprocs=8 idleprocs=6 threads=10 spinningthreads=1 idlethreads=3 runqueue=0 [0 0 0 0 0 0 0 0]
```

| Field | Meaning |
|-------|---------|
| `gomaxprocs` | GOMAXPROCS value |
| `idleprocs` | Idle Ps |
| `threads` | Total OS threads |
| `spinningthreads` | Threads looking for work |
| `runqueue` | Global run queue length |
| `[0 0 ...]` | Per-P local run queue lengths |

## Runtime Profiling

```go
import "runtime/trace"

f, _ := os.Create("trace.out")
trace.Start(f)
defer trace.Stop()
// ... code to trace ...
```

Then analyze:
```bash
go tool trace trace.out
```

## Output Format

```
## Runtime Analysis

## Environment
- GOMAXPROCS: N
- GODEBUG: <settings used>

## GC Behavior
| Metric | Value | Assessment |
|--------|-------|------------|
| GC frequency | X/s | <normal/high/low> |
| Avg pause | Xms | <acceptable/concerning> |
| Heap growth | X MB | <stable/growing> |

## Scheduler Behavior
| Metric | Value | Assessment |
|--------|-------|------------|
| Goroutines | N | <info> |
| Idle procs | M | <info> |
| Run queue | K | <info> |

## Issues Identified
1. <issue description>
2. <issue description>

## Recommendations
1. <suggestion>
2. <suggestion>
```

## Runtime Source Reference

Key files in `runtime/`:
- `proc.go` - Scheduler
- `mgc.go` - GC coordination
- `malloc.go` - Memory allocator
- `runtime2.go` - Core data structures (G, M, P)

## Example Usage

```
/runtime-trace gc-analysis
/runtime-trace scheduler ./myprogram
/runtime-trace memory-profile
```
