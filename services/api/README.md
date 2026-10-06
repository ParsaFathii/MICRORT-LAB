# MicroRT-Lab Simulation API (services/api)

FastAPI analysis layer for the MicroRT-Lab deterministic real-time scheduling
engine: experiment management, simulation execution, trace analysis, RT
feasibility theory, comparisons and reports. Python 3.12, FastAPI, pydantic v2,
SQLite (stdlib). No other runtime dependencies.

Copyright © 2026 Parsa Fathi — Apache-2.0.

## Run

```bash
cd services/api
bash run-dev.sh                     # == uvicorn app.main:app --host 127.0.0.1 --port 3031 --reload
# or directly:
python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031
```

Interactive docs: `http://127.0.0.1:3031/docs`.

The C++ engine binary must be built (from the repo root):

```bash
bash scripts/build-native.sh        # → engine/build/engine/micrort-engine
```

If the binary is missing, the service still starts but `/api/v1/health`
reports `engine.available=false` and every run endpoint answers **503** with
`engine binary not built — run scripts/build-native.sh`.

Sandbox note: the gateway launches this service from
`mini-services/sim-api` (bun wrapper). Frontend calls go through the gateway
as relative `/api/v1/...?XTransformPort=3031` paths; the direct base URL is
`http://127.0.0.1:3031`.

## Environment variables

| Variable                 | Default (relative to `services/api`)        | Meaning            |
| ------------------------ | ------------------------------------------- | ------------------ |
| `MICRORT_ENGINE_BIN`     | `../../engine/build/engine/micrort-engine`  | engine binary      |
| `MICRORT_DATA_DIR`       | `./data`                                    | SQLite data dir    |
| `MICRORT_EXPERIMENTS_DIR`| `../../experiments`                         | built-in experiments |

## API overview (`/api/v1`)

### Simulations

| Method | Path | Description |
| ------ | ---- | ----------- |
| POST   | `/simulations` | create from `{name?, config}` (pydantic-validated) |
| GET    | `/simulations?limit&offset` | paginated summaries |
| GET    | `/simulations/{id}` | full record incl. result document |
| POST   | `/simulations/{id}/run` | run the engine synchronously, store result |
| POST   | `/simulations/{id}/stop` | terminate the active engine run (409 if not running) |
| DELETE | `/simulations/{id}` | delete record + its reports |
| GET    | `/simulations/{id}/trace?type&task&res&limit&offset` | trace events, sorted by (t, seq) |
| GET    | `/simulations/{id}/metrics` | engine metrics + Python recomputation + deltas |
| GET    | `/simulations/{id}/tasks` | per-task accounting arrays |
| GET    | `/simulations/{id}/gantt` | gantt segments |
| GET    | `/simulations/{id}/memory` | memory section (events, layout, fragSeries) |
| GET    | `/simulations/{id}/deadlocks` | detected RAG cycles |
| GET    | `/simulations/{id}/analysis` | `rt` + `starvation` + `fragmentation` |
| GET    | `/simulations/{id}/report?format=markdown\|csv\|json` | generated (and stored) report |

### Experiments

| Method | Path | Description |
| ------ | ---- | ----------- |
| GET    | `/experiments` | 9 built-ins (experiments/*.json) + custom (DB) |
| GET    | `/experiments/{id}` | full experiment record (id = file stem or short uuid) |
| POST   | `/experiments` | store a custom experiment (pydantic + engine validated) |
| POST   | `/experiments/{id}/run` | create + run a simulation from the experiment |
| POST   | `/experiments/{id}/validate` | engine validate verdict |

### Comparisons

| Method | Path | Description |
| ------ | ---- | ----------- |
| POST   | `/comparisons` | `{name?, config \| experimentId, variants: [{label, scheduler?, memory?, aging?}]}` |
| GET    | `/comparisons?limit&offset` | paginated summaries |
| GET    | `/comparisons/{id}` | full record (per-variant metrics, best-per-metric, deltas) |
| GET    | `/comparisons/{id}/report` | Markdown report |

### Reports / health

| Method | Path | Description |
| ------ | ---- | ----------- |
| GET    | `/reports?simulationId=\|comparisonId=&limit&offset` | stored reports |
| GET    | `/reports/{id}?format=` | stored report content (400 on format mismatch) |
| GET    | `/health` | status, engine availability (30 s cache), row counts |

## Conventions

- **IDs**: `uuid4().hex[:12]`; timestamps ISO 8601 UTC (`...Z`).
- **Lists**: `{"items": [...], "total": n, "limit": l, "offset": o}`; everything
  else is flat JSON.
- **Errors**: unknown id → `404 {"error", "details"}`; invalid input → pydantic
  `422` with field paths; engine validation failure at run time → `422` with
  engine details; engine runtime failure/timeout → `502` with stderr details;
  engine binary missing → `503`; data endpoints before a run → `409`.
- **Simulation status** (`created | running | completed | failed | stopped`) is
  the *API-level* lifecycle. The engine's own result `status`
  (`completed | stopped` — horizon reached with work pending) stays inside the
  result document and still maps to an API-level `completed` run.
- **Stop semantics**: `POST .../stop` terminates the engine subprocess; the
  in-flight `/run` request then finalizes the record as `stopped`.
- **Comparison merge semantics**: each variant's `scheduler`/`memory`/`aging`
  objects are **deep-merged** over the base config — nested dicts merge
  recursively, lists and scalars are replaced wholesale. The merged config is
  re-validated (pydantic) before the engine run, so determinism is preserved
  (identical base + overrides ⇒ identical result modulo `wallMicros`).
- **No global mutable state** beyond the store's threading lock and the
  EngineRunner's active-process registry. All handlers are sync (`def`) and run
  in the threadpool; the engine never blocks the event loop forever (60 s
  timeout, hard kill).
- Config pydantic models mirror `docs/spec/SIMULATION_SCHEMA.md` exactly —
  including the engine's cross-field rules (`jitter`/`period` only on periodic,
  `releases` only on sporadic, `quantum` only for `rr`, op↔resource kind
  matching, `alloc`/`free` require a `memory` block). `free`-tag matching and
  lock ownership are engine run-time checks.

## Analysis layer

- `recompute_metrics(result)` — recomputes `avgWaiting/avgTurnaround/
  avgResponse/cpuUtilization/throughput` from **trace + gantt alone**
  (schema §Metric formulas) and reports per-metric deltas vs the engine
  (tolerance 1e-3). Job semantics mirror the engine: wait/turnaround exist
  only for completed jobs, response for every dispatched job.
- `rm_utilization_bound(n) = n*(2^(1/n)-1)` — Liu & Layland bound.
- `rt_analysis(config, result)` — per-task utilization (WCET proxy = Σ cpu
  step durations), total U, Liu & Layland bound, hyperbolic bound
  Π(Uᵢ+1) ≤ 2, kept strictly separate from the measured outcome
  (deadline misses, completion, starvation).
- `starvation_report(result, threshold=100, cpu_ratio=5)` — engine heuristic
  re-applied with configurable threshold.
- `fragmentation_summary(result)` — allocator outcome (peaks, failures, leaks,
  final free fragmentation).

## Tests

```bash
cd services/api
python3 -m ruff check app tests
python3 -m pytest            # 97 tests — uses the REAL engine binary
```
