# Development environment & sandbox setup (SETUP.md)

How the MicroRT-Lab development environment is wired: ports, the gateway
convention, dev commands, the mini-service wrapper, the agent worklog protocol
and repo hygiene rules.

This file documents the **development sandbox** in which MicroRT-Lab is built. The
product itself (engine, API, UI) runs fine in a plain checkout — see the root
[README.md](../../README.md) for the generic install path.

## Ports and the gateway convention

| Port | Service | Access |
|---|---|---|
| 3000 | Next.js dev server (`bun run dev`) | the only externally exposed port; in the sandbox the gateway maps it to external port **81** |
| 3031 | FastAPI analysis service (uvicorn) | **never exposed directly**; the web app reaches it via the gateway using the `XTransformPort` convention |
| 81 | sandbox gateway | external entry point that serves the dev server and forwards `/api/v1/*` |

**The `XTransformPort` convention:** the browser can only talk to the exposed web
origin. The workstation therefore issues **relative** API requests of the form

```
GET/POST  /api/v1/...?XTransformPort=3031
```

and the gateway forwards them to `127.0.0.1:3031`. Rules that follow from this:

- never build absolute URLs to `127.0.0.1:3031` into the frontend;
- never call other ports from the browser;
- the API client (`src/lib/api.ts`) appends `XTransformPort=3031` to `/api/v1`
  paths and handles pre-existing query strings — reuse `buildUrl`, don't
  hand-roll URLs.

Direct `curl` during development uses `http://127.0.0.1:3031/api/v1/...` (fine
from a shell, not from the browser).

## Bringing the stack up

```bash
# 1. native engine (needed before anything can run)
bash scripts/build-native.sh              # → engine/build/engine/micrort-engine

# 2. analysis API (the sandbox gateway launches this via the mini-service wrapper,
#    but you can run it yourself)
cd services/api
python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031 --reload
# (equivalent: bash run-dev.sh)

# 3. web workstation
bun run dev                               # port 3000, logs to dev.log
```

## Dev commands & files

| Command | Purpose |
|---|---|
| `bash scripts/build-native.sh` | cmake Release build of kernel + engine into `engine/build` |
| `ctest --test-dir engine/build` | 11 C test suites (5 kernel + 6 engine) |
| `engine/build/engine/micrort-engine selftest` | 54 internal invariant checks |
| `cd services/api && python3 -m pytest` | 97 API tests (real engine binary) |
| `cd services/api && python3 -m ruff check app tests` | Python lint |
| `bun run lint` | eslint for the web app (build dirs are ignored) |
| `bun run dev` | Next.js dev server on 3000 |

- **`dev.log`** — the dev server writes its output here (the `dev` script pipes
  through `tee dev.log`). First stop for "is the web app up?": the last lines
  should show a successful compile and `GET / 200`.
- **`engine/build/`** and **`kernel/build/`** — CMake build trees; the binary
  lives at `engine/build/engine/micrort-engine` (CMake subdir layout).

## The mini-services/sim-api wrapper (sandbox only)

The sandbox gateway starts background services from `mini-services/`. The Python
API is launched through `mini-services/sim-api/`, a tiny **bun package that only
exists to satisfy the sandbox service-runner convention**:

```json
{
  "name": "micrort-sim-api",
  "private": true,
  "scripts": {
    "dev": "cd ../../services/api && exec python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031 --reload"
  }
}
```

- The wrapper contains no application code — the real service is
  `services/api/`.
- **The whole `mini-services/` directory is gitignored** and never part of the
  repository; it exists only in the sandbox so the gateway can (re)start the API.
- If you need the API running and the gateway hasn't started it, run it directly
  as shown above.

## The worklog protocol (agents and long tasks)

`worklog.md` at the repository root (gitignored — it is a development artifact,
not product documentation) is the shared memory for the engineering agents
working in this environment:

1. **Read it completely before starting any task** — earlier sections are the
   factual record of what was built, decided and verified.
2. **Append your section after finishing** — never rewrite or reorder existing
   sections; the file is append-only.
3. Sections follow the existing format: task id, agent, task, work log (steps +
   verified numbers), stage summary (files owned, facts, hand-off notes for the
   next agent).
4. Record *measured* numbers (test counts, timings, output shapes) rather than
   claims; later agents rely on them.

Product documentation that should outlive the sandbox (README, docs/, specs)
must never be replaced by the worklog — the worklog is for the build process,
the docs are for users.

## Repo hygiene — what is gitignored and why

From `.gitignore` (project-relevant entries):

| Ignored | Why |
|---|---|
| `engine/build/`, `kernel/build/`, `build/`, `*.o`, `*.a`, `*.so` | CMake build artifacts, regenerated by `scripts/build-native.sh` |
| `services/api/data/`, `*.db`, `*.db-journal`, `data/results/`, `results/` | runtime data (SQLite store, generated results) — simulation outputs are reproducible, not source |
| `dev.log`, `dev.out.log`, `server.log`, `*.log` | dev-server and runtime logs |
| `mini-services/` | sandbox-only service wrappers (see above) |
| `worklog.md` | the agent worklog (development artifact, not product doc) |
| `tool-results/`, `.zscripts/`, `upload/`, `download/`, `examples/`, `test`, `prompt`, `db/`, `.claude`, `.z-ai-config`, `local-*` | sandbox/tooling scratch directories and local configuration |
| `.env*` | environment files must never be committed (no secrets in the repo) |
| `node_modules`, `.next/`, `out/`, `coverage`, `*.tsbuildinfo` | standard JS build outputs |
| `__pycache__/`, `*.pyc`, `.venv/`, `.pytest_cache/`, `.ruff_cache/` | Python caches |

Rules of thumb:

- nothing generated goes into git; everything generated is either rebuilt by a
  script or reproducible from a config + seed;
- no credentials or tokens in the repo (git credential material stays outside the
  tree; `.env*` is ignored by default);
- `docs/spec/*` are frozen contracts — changes there follow the documentation-first
  rule in [CONTRIBUTING.md](../../CONTRIBUTING.md).
