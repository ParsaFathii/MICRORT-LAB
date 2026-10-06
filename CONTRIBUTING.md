# Contributing to MicroRT-Lab

Thank you for considering a contribution. This document describes the project layout,
the conventions each layer follows, what is expected of tests, and how changes move
from idea to merged commit.

Copyright © 2026 Parsa Fathi — contributions are accepted under the repository's
[Apache-2.0 license](LICENSE).

## Project map

```
kernel/          C11 kernel-style primitives — the simulation's low-level model.
                 include/micrort/*.h is the frozen C API contract the engine consumes.
engine/          C++20 discrete-event simulation engine + CLI (micrort-engine).
                 Vendored nlohmann/json (MIT) under engine/third_party/.
services/api/    Python 3.12 FastAPI layer: experiments, runs, analysis, comparisons,
                 reports, SQLite storage. Tests use the real engine binary.
src/             Next.js 16 workstation UI (single / route, 11 views, no routing).
experiments/     9 canonical experiment configs (micrort-config/1) + README.
scripts/         build-native.sh (cmake build of kernel + engine).
docs/spec/       Frozen contracts — read before touching any layer:
                   SIMULATION_SCHEMA.md   data contract (config/result/trace/rules)
                   FRONTEND_ARCHITECTURE.md view layout, store, design tokens
docs/fa/         Persian documentation set.
docs/dev/        Sandbox/dev environment notes (SETUP.md).
```

Also relevant: the root `CMakeLists.txt` calls `enable_testing()` **before**
`add_subdirectory(...)` — keep that order if you touch the build.

## Development setup

```bash
git clone https://github.com/ParsaFathii/MICRORT-LAB.git && cd MICRORT-LAB

# native (kernel + engine) — gcc 14+/clang, CMake >= 3.16
bash scripts/build-native.sh           # → engine/build/engine/micrort-engine

# analysis API — Python 3.12+
cd services/api
python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031   # or: bash run-dev.sh

# web workstation — bun 1.1+ (or Node 20+)
cd ..
bun install && bun run dev             # port 3000
```

Environment specifics (sandbox gateway, port conventions, dev.log, the worklog
protocol) are documented in [docs/dev/SETUP.md](docs/dev/SETUP.md).

## Documentation-first rule

`docs/spec/SIMULATION_SCHEMA.md` is the single source of truth for the data contracts.
When a change alters what any layer emits or accepts:

1. update `docs/spec/SIMULATION_SCHEMA.md` **first** (and
   `docs/spec/FRONTEND_ARCHITECTURE.md` if views/store/API-client shape changes),
2. then the engine,
3. then the pydantic mirror in `services/api/app/models.py`,
4. then `src/lib/types.ts` and the affected views,
5. then the tests, and finally the prose docs (README.md, docs/fa/).

A PR that changes output shapes without updating the spec will be asked to do so.

## Coding conventions

### C — kernel/

- Pure C11, fixed-capacity arrays, **no dynamic allocation**, no engine/UI coupling.
- Zero warnings under `-Wall -Wextra -Wpedantic`.
- Errors are `mrt_result_t` codes; no longjmp, no globals.
- Headers document contracts (preconditions, result codes, ownership); see
  `kernel/include/micrort/mrt_sync.h` for the expected density of commentary.
- Tests: `kernel/tests/*.c` with the in-repo test harness (`CHECK`-style counters);
  each suite prints its check count and exits non-zero on failure.
- Kernel API subtleties the engine must respect (documented in the headers): woken
  tasks are set READY by sync calls but are **not** pushed into the ready queue — the
  engine pushes on wake and pops on dispatch; timer handles are invalidated by list
  mutation (rescan, don't cache); evflags wait parameters are packed in
  `task->engine_ctx` while blocked.

### C++ — engine/

- C++20, RAII, **no globals**, and **no unordered containers** on output paths —
  everything that reaches the result document iterates declaration order or ordered
  maps, because the result must be byte-identical for identical (config, seed).
- Headers state class/function contracts (`engine/include/micrort/simulation.hpp`
  documents the event-loop phase order, `scheduler.hpp` the RM priority mapping).
- CLI contract (exit codes 0/2/3, stderr JSON errors, `--out -`) is part of the
  schema; don't change it casually.
- Tests: `engine/tests/*` cover config validation, scheduler selection, CLI, the
  simulation core, trace/gantt invariants and metrics formulas.

### Python — services/api/

- Fully type-annotated; `ruff` (line-length 100, rules E/W/F/I/UP/B) must be clean.
- Handlers are sync `def` (threadpool); no global mutable state beyond the store's
  lock and the EngineRunner's active-process registry.
- Pydantic models must mirror the schema exactly, including cross-field rules
  (`jitter`/`period` only on periodic, `releases` only on sporadic, `quantum` only
  for `rr`, op↔resource kind matching, `alloc`/`free` require a `memory` block).
- Tests: pytest against the **real engine binary** (missing binary should fail your
  test run — build it first).

### TypeScript — src/

- Strict mode, **no `any`** — use `unknown` with type guards.
- All fetches go through `src/lib/api.ts` (timeouts, typed `ApiError`); no raw fetch
  in components.
- New views: create `src/components/views/<Name>View.tsx`, register it in
  `src/components/views/registry.tsx` (and add the id to `VIEW_IDS` in the store if
  new). The shell derives the rail/tooltips/keyboard map from the registry — no shell
  edits needed.
- Colors come from the workstation design tokens in `globals.css`; no blue/indigo,
  amber = running/primary, red = blocked/danger.
- Query keys: `['health']`, `['experiments']`, `['simulations']`, `['sim',id]`,
  `['comparison',id]`, `['reports',...]`.
- The UI never computes scheduler results; derived client-side series (e.g. the
  ready-queue length chart) must be labeled as derived.

## Testing expectations

Every subsystem is developed implement → test → fix → retest; keep that loop honest:

| Layer | Command (repo root) | Must pass |
|---|---|---|
| kernel + engine | `ctest --test-dir engine/build` | 11/11 suites (5 kernel + 6 engine) |
| engine invariants | `engine/build/engine/micrort-engine selftest` | `{"checks":54,"selftest":"ok"}` |
| experiments | run all `experiments/*.json` | each `status` valid and its documented outcome present |
| Python API | `cd services/api && python3 -m pytest` | 97 tests |
| Python lint | `cd services/api && python3 -m ruff check app tests` | clean |
| Web | `bun run lint` (and `bunx tsc --noEmit` for src) | clean |

New behavior needs new tests: a scheduler change extends `test_scheduler`, an output
field extends `test_trace_gantt`/`test_metrics` plus the pydantic mirror tests, an
endpoint change extends the `test_api_*` suites. Bug fixes come with a regression
test that fails without the fix.

Determinism is a feature — if your change makes two identical runs differ by anything
other than `wallMicros`, it is a bug, not an optimization.

## Commit and PR workflow

- Small commits with conventional prefixes: `feat:`, `fix:`, `docs:`, `ci:`, `test:`,
  `refactor:`, `chore:`.
- One logical change per PR; describe what changed and why, referencing the schema
  section when contracts move.
- PRs must keep **ctest + pytest + ruff + web lint** green (CI runs the same set).
- Update the affected experiments/README tables and prose docs in the same PR when
  observable behavior changes.
- After review squash-and-merge or rebase-merge as appropriate; the commit history
  should read like a changelog.

## Reporting issues

Include: the layer involved (kernel / engine / api / web), the engine binary presence
(`micrort-engine schema` output), a **minimal config JSON** that reproduces the
problem, the exact command and exit code, and for the API the status code + response
body. For determinism bugs, attach both result documents.

## Licensing

- Your contributions are accepted under **Apache-2.0** (see [LICENSE](LICENSE)); you
  retain your copyright via the contribution terms of the license.
- Third-party code must be attributed: add the component, version, license, source
  URL and attribution requirement to [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
  in the same PR that introduces it. Prefer vendoring small single-file dependencies
  (as done for nlohmann/json) so the build stays self-contained.
- Do not commit secrets, tokens or credentials; the repository is scanned.
