# Canonical experiments

Each file in this directory is a valid experiment configuration
(`micrort-config/1`, see `docs/spec/SIMULATION_SCHEMA.md`). The Python
analysis layer ships them as the built-in experiment library; the engine
must execute every one of them and the expected educational outcome must
actually be observable in the result document (trace/metrics/anomalies).

| file | demonstrates | expected observable outcome |
|---|---|---|
| `rr-quantum-comparison.json` | Round Robin quantum sensitivity | same workload, quantum 2 vs 8 — different response/ctx-switch profile |
| `priority-starvation.json` | starvation under preemptive priority | low-priority task flagged `starved`, long READY wait, aging variant rescues it |
| `priority-inversion.json` | priority inversion (no inheritance) | High BLOCKED while Medium runs; High response inflated |
| `priority-inheritance.json` | same scenario with protocol `inherit` | Low boosted (LOCK_INHERIT events), High completes sooner |
| `producer-consumer.json` | bounded buffer via msgq | producers/consumers alternate; contentions visible; no deadlock |
| `deadlock-circular.json` | circular wait | `deadlocks[]` non-empty, cycle [A,M1,B,M2], DEADLOCK trace events |
| `memory-fragmentation.json` | first vs best fit fragmentation | fragmentation metrics + alloc failures with first fit pattern |
| `edf-deadlines.json` | EDF meeting deadlines at U>0.69 | zero deadline misses under EDF where fixed priority would miss |
| `rate-monotonic.json` | RM scheduling + utilization bound | jobs meet deadlines while U <= n(2^{1/n}-1) |

Verification (must pass before experiments are considered done):

```bash
for f in experiments/*.json; do
  engine/build/micrort-engine run --config "$f" --out /tmp/$(basename "$f").result.json
done
```

Each result: `status: completed`, no validation errors, and the expected
outcome above is present in the result document.
