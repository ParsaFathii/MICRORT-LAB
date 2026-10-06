# MicroRT-Lab Simulation Schema — v1 (frozen contract)

This document is the **single source of truth** for the data contracts between
the C++ engine, the Python analysis layer and the web frontend. Every layer
implements exactly what is written here. Changes require updating this file
first, then every layer, then the tests.

- [Experiment configuration](#experiment-configuration)
- [Result document](#result-document)
- [Trace events](#trace-events)
- [State semantics](#state-semantics)
- [Determinism & tie-breaking rules](#determinism--tie-breaking-rules)
- [Engine CLI contract](#engine-cli-contract)
- [Metric formulas](#metric-formulas)

---

## Experiment configuration

```json
{
  "schema": "micrort-config/1",
  "name": "priority-inversion-demo",
  "description": "Classic inversion: Low holds M1 while High waits and Medium preempts Low.",
  "seed": 0,
  "duration": 120,                    // simulation horizon in ticks (>= 1, <= 100000)
  "cpus": 1,                          // only 1 supported in v1 (validated)
  "contextSwitchCost": 0,             // ticks charged on every CPU handover (0..10)
  "scheduler": {
    "type": "priority_p",             // fifo | rr | priority | priority_p | sjf | srtf | rm | edf
    "quantum": 4                      // rr only, 1..1000; default 4
  },
  "aging": {                          // optional; effective for priority* schedulers
    "interval": 20,                   // every N ticks in READY -> +1 effective prio
    "cap": 10                         // max boost above base priority
  },
  "memory": {                         // optional; omit to disable memory model
    "model": "region",                // region | pool
    "total": 1024,                    // region: total bytes; pool: block_count * block_size
    "policy": "first_fit",            // first_fit | best_fit | worst_fit (region only)
    "blockSize": 64,                  // pool only
    "blockCount": 16                  // pool only
  },
  "tasks": [ ... ],
  "resources": [ ... ]
}
```

### Task object

```json
{
  "id": "High",                        // unique, 1..31 chars [A-Za-z0-9_-]
  "name": "High Control Loop",         // optional display name (defaults to id)
  "kind": "periodic",                  // aperiodic | periodic | sporadic
  "priority": 8,                       // -100..100, higher = more urgent (default 0)
  "arrival": 0,                        // first release tick (>= 0)
  "period": 40,                        // periodic only, >= 1
  "relativeDeadline": 30,              // ticks from job release; 0/absent = none
  "jitter": 0,                         // periodic release jitter 0..period-1, seeded PRNG
  "releases": [0, 15, 37],             // sporadic only: explicit sorted release ticks
  "steps": [ ... ]                     // job program, >= 1 step
}
```

Job semantics: every release creates one **job** that executes `steps` once.
A task with `kind: "aperiodic"` has exactly one job (at `arrival`).

### Program steps

| op            | fields                    | semantics                                                                 |
|---------------|---------------------------|---------------------------------------------------------------------------|
| `cpu`         | `d` (1..100000)           | CPU burst of `d` ticks                                                    |
| `io`          | `d`, optional `dev` label | Blocks task for `d` ticks (I/O device, state WAITING)                     |
| `sleep`       | `d`                       | Timed sleep (state SLEEPING)                                              |
| `lock`        | `res`                     | Acquire mutex; blocks (BLOCKED) while held                                 |
| `unlock`      | `res`                     | Release mutex (must be owner; validation error otherwise)                  |
| `wait`        | `res`                     | Semaphore P: consume token or block                                        |
| `signal`      | `res`                     | Semaphore V: wake highest-prio waiter or increment                          |
| `send`        | `res`, `msg` (int)        | Message queue send; blocks when full                                        |
| `recv`        | `res`                     | Message queue receive; blocks when empty; consumes payload                 |
| `evwait`      | `res`, `mask`, `mode`     | Wait for event flags; mode `"any"` (default) or `"all"`; blocks if unsatisfied |
| `evset`       | `res`, `mask`             | OR bits into flags; wakes satisfied waiters                                |
| `alloc`       | `size`, optional `tag`    | Allocate `size` bytes from memory model (fails -> MEM_ALLOC_FAIL, task continues) |
| `free`        | `tag`                     | Free the allocation made by the matching `alloc` tag (default tag: `"a<stepIndex>`") |

Notes:
- `alloc`/`free` require a `memory` block in the configuration.
- A job that finishes with live allocations leaves them allocated (leak) —
  reported in `memory.leaks`.
- `free` with an unknown tag is a validation error (checked at run start).

### Resource object

```json
{ "id": "M1", "type": "mutex",   "protocol": "inherit" }          // none | inherit
{ "id": "S1", "type": "sem",     "initial": 0, "max": 3 }          // initial 0..max, max >= 1
{ "id": "Q1", "type": "msgq",    "capacity": 4 }                   // >= 1
{ "id": "E1", "type": "evflags" }
```

### Validation rules (engine `validate` + Python pydantic both enforce)

- unique task ids, unique resource ids; resource references must exist
- `steps` non-empty; `d` >= 1 for cpu/io/sleep
- rr requires quantum 1..1000; `cpus` == 1; durations 1..100000
- periodic needs `period` >= 1; sporadic needs non-empty sorted `releases`
- tasks with deadlines under `rm`/`edf`: periodic tasks strongly recommended
  (aperiodic tasks under rm get lowest priority, under edf deadline = arrival + relativeDeadline
  or infinite when no deadline)
- task count <= 64; resources <= 32; steps per job <= 256; total trace events <= 500000

---

## Result document

`micrort-engine run` writes (and stdout echoes) this JSON:

```json
{
  "schema": "micrort-result/1",
  "config": { ...echo of the experiment configuration... },
  "configHash": "sha256 of canonical (sorted-key, whitespace-free) config JSON, first 16 hex chars",
  "seed": 0,
  "status": "completed",              // completed | stopped (max events/duration reached with work pending)
  "simulatedUntil": 118,              // last tick processed (<= duration)
  "wallMicros": 421,                  // engine wall time for the run (NOT simulation time)

  "scheduler": { "type": "priority_p", "quantum": null },

  "tasks": [ {
    "id": "High", "name": "High Control Loop", "kind": "periodic",
    "priority": 8, "arrival": 0, "period": 40, "relativeDeadline": 30,
    "jobsReleased": 3, "jobsCompleted": 3, "deadlineMisses": 0,
    "cpuTime": 24, "readyWaitTotal": 12, "blockedTotal": 9,
    "waitingTotal": 0, "sleepingTotal": 0,
    "firstStart": 2, "lastFinish": 110,           // -1 when never started / never finished
    "responseTimes": [2, 1, 3],                   // release -> first dispatch, per job
    "turnaroundTimes": [18, 17, 20],              // release -> completion, per job
    "waitTimes": [14, 13, 16],                    // total READY residence per job
    "starved": false
  } ],

  "metrics": {
    "avgWaiting": 13.0, "avgTurnaround": 18.3, "avgResponse": 2.0,
    "throughput": 0.05,               // jobs completed per tick
    "completedJobs": 9, "releasedJobs": 9,
    "completionRate": 1.0,
    "cpuUtilization": 0.83,           // busy / simulatedUntil
    "idleTime": 20, "ctxSwitches": 11, "preemptions": 4,
    "ctxOverheadTime": 0,
    "avgQueueLen": 1.2, "maxQueueLen": 4,
    "deadlineMisses": 0, "blockedTimeTotal": 9,
    "memoryUtilizationAvg": 0.62,     // region/pool used fraction averaged over alloc/free events
    "memoryPeak": 704, "fragmentationAvg": 48, "fragmentationMax": 96,
    "allocFailures": 0, "totalEvents": 214
  },

  "trace": [ { "seq": 0, "t": 0, "type": "SIM_START", "task": null, "res": null,
               "from": null, "to": null, "dur": null, "detail": {} }, ... ],

  "gantt": [ { "task": "High", "t0": 2, "t1": 5, "state": "RUNNING", "note": null },
             { "task": "Low",  "t0": 5, "t1": 12, "state": "RUNNING", "note": "ctx" } ],

  "resources": [ { "id": "M1", "type": "mutex", "finalOwner": null,
                   "acquisitions": 3, "contentions": 1,
                   "holderAtEnd": null, "queueDepth": 0 } ],

  "deadlocks": [ { "t": 61, "cycle": [0, 1, 1, 0],
                   "tasks": ["A", "B"], "resources": ["M1", "M2"] } ],
  // cycle: alternating engine task INDEX / resource INDEX pairs starting
  // and ending at a task index (e.g. [A0, M1_1, B1, M2_0...]); the
  // tasks/resources arrays carry the readable id names in cycle order.

  "memory": {
    "model": "region", "total": 1024, "policy": "first_fit",
    "events": [ { "t": 3, "op": "alloc", "task": "A", "tag": "a3", "size": 128,
                  "result": "ok", "offset": 0 } ],
    "finalLayout": [ { "offset": 0, "size": 128, "owner": "A", "tag": "a3" },
                     { "offset": 128, "size": 896, "owner": null } ],
    "failures": [], "leaks": [], "peakUsage": 640,
    "fragSeries": [ { "t": 3, "used": 128, "free": 896, "largest": 896, "frag": 0 } ]
  },

  "anomalies": [ { "t": 61, "type": "deadlock", "tasks": ["A","B"],
                   "resources": ["M1","M2"], "detail": "circular wait" } ],

  "schedulerNotes": [ { "t": 0, "chosen": "Low", "reason": "only ready task" } ]
}
```

Empty arrays are always present (never omitted). `null` for absent task/res.
All times are integer ticks; averages can be fractional (rounded to 3 decimals).

---

## Trace events

`type` is one of (stable string values — UI and CSV map onto these):

SIM_START, SIM_END, TASK_ARRIVAL (job release; detail: {job}),
JOB_RELEASE (alias of TASK_ARRIVAL emitted for periodic/sporadic jobs),
DISPATCH (task chosen to run; detail: {reason}),
PREEMPT (running task loses CPU to a more urgent one),
QUANTUM_EXPIRE (rr quantum exhausted; task -> READY),
CONTEXT_SWITCH (CPU handover; detail: {from, to, cost}),
CPU_END (cpu burst finished),
IO_START / IO_END (detail: {dev, d}),
SLEEP_START / SLEEP_END,
LOCK_ACQUIRE (detail: {res}),
LOCK_BLOCK (task blocks on mutex; detail: {res, owner}),
LOCK_RELEASE (detail: {res, handedTo}),
LOCK_INHERIT / LOCK_UNINHERIT (detail: {res, from, to}), // priority values
SEM_WAIT (detail: {res, acquired: bool}),
SEM_SIGNAL (detail: {res, woken: id|null, overflow: bool}),
MSG_SEND (detail: {res, msg, blocked: bool}),
MSG_RECV (detail: {res, msg, blocked: bool}),
EV_WAIT (detail: {res, mask, mode, satisfied}),
EV_SET (detail: {res, mask, woken: [ids]}),
MEM_ALLOC (detail: {size, tag, offset, ok}),
MEM_FREE (detail: {tag, offset, size}),
DEADLOCK (detail: {cycle: [ids]}),
DEADLINE_MISS (detail: {job, deadline, finished}),
TASK_COMPLETE (job finished; detail: {job}),
AGING_BOOST (detail: {from, to}), // effective priority values
TIMER_EXPIRE (detail: {tag}).

Every record: `{seq, t, type, task, res, from, to, dur, detail}` where
`from`/`to` are task states around the event (when applicable) and `dur`
carries the burst length when applicable. `seq` is global and strictly
increasing; records are sorted by `(t, seq)`.

---

## State semantics

| state      | meaning                                                    |
|------------|------------------------------------------------------------|
| READY      | runnable, waiting in the ready queue                        |
| RUNNING    | executing on the (single) CPU                               |
| BLOCKED    | waiting on a resource: mutex, semaphore, message queue, event flags |
| WAITING    | I/O burst in flight (timed wait on a device)                 |
| SLEEPING   | explicit sleep                                              |
| TERMINATED | job completed (aperiodic tasks end here; periodic tasks return to the release schedule) |

A job ends TERMINATED; the task itself stays schedulable for future releases.

---

## Determinism & tie-breaking rules

1. Time advances event-to-event (discrete event simulation). No wall-clock.
2. Equal-priority ready tasks dispatch FIFO (kernel `enqueue_seq`).
3. Event processing at the same tick happens in this order:
   a) timers due (IO_END, SLEEP_END, TIMER_EXPIRE, DEADLINE markers) — in
      insertion order;
   b) arrivals/job releases (in task declaration order);
   c) completions/unblocks already queued at this tick (in `seq` order);
   d) exactly one scheduling decision point at the end of the tick batch.
4. Preemption check happens at every DISPATCH-relevant state change
   (arrival, unblock, quantum expiry, burst end) and only switches when a
   strictly more urgent candidate exists (no thrash on ties).
5. Context switch: when the CPU changes task (A -> B, A != B), the engine
   inserts `contextSwitchCost` ticks of CPU unavailability, traced as
   CONTEXT_SWITCH (gantt note "ctx").
6. `seed` drives a xoshiro256** PRNG used ONLY for periodic release jitter
   (> 0). seed 0 = fully static schedule. Same config + seed => byte-identical
   result document (modulo `wallMicros`, which is excluded from equality).
7. SJF/SRTF compare remaining CPU demand of the current job; ties -> FIFO.
8. RM: static priority = 1/period (shorter period = more urgent); aperiodic
   tasks get priority MRT_PRIO_MIN. EDF: earlier absolute deadline wins;
   no deadline = MRT_TIME_MAX; ties -> FIFO.

---

## Engine CLI contract

```
micrort-engine run       --config <file|-> [--out <file|->] [--seed N]
micrort-engine validate  --config <file|->
micrort-engine schema                     # prints this schema summary as JSON
micrort-engine selftest                   # runs internal invariants, exit 0
```

- exit 0 = success; 2 = validation error (JSON error on stderr:
  `{"error": "validation", "details": [...]}`); 3 = runtime failure.
- `run` writes the result document to `--out` (or stdout when `-`) and a
  one-line summary to stderr.
- Build: `cmake -S . -B engine/build && cmake --build engine/build` —
  binary at `engine/build/micrort-engine`; kernel tests via
  `ctest --test-dir engine/build`.

---

## Metric formulas (documented; Python recomputes from trace for cross-check)

- waitingTime(job) = Σ READY residence for that job
- turnaround(job)  = completionTick − releaseTick
- response(job)    = firstDispatchTick − releaseTick
- throughput        = completedJobs / simulatedUntil
- cpuUtilization    = (Σ RUNNING segment lengths) / simulatedUntil
- avgQueueLen       = time-weighted mean of ready-queue length
- memoryUtilization = used/total, averaged over the alloc/free event series
- fragmentation     = freeTotal − largestFreeBlock (region) at each event
- starvation flag   = task waited READY ≥ 100 ticks with zero CPU progress
  while other tasks ran (threshold configurable in analysis layer)

Python must be able to recompute every per-job and aggregate metric from
`trace` + `gantt` alone; a pytest asserts this cross-check.
