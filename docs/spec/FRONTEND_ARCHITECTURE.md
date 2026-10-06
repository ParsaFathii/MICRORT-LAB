# MicroRT-Lab Frontend Architecture — v1 (frozen contract)

Single-route simulation workstation (Next.js 16 App Router, `/` only).
All views are client components switched via a Zustand store — no routing.
The UI **never computes scheduler results**; it visualizes trace data
produced by the C++ engine through the Python API.

## Data access

- Python API base: relative URL `/api/v1/…?XTransformPort=3031` (gateway
  forwards to the FastAPI service; never absolute URLs, never other ports).
- Typed client: `src/lib/api.ts` (fetch wrapper, error normalization).
- Types mirroring `docs/spec/SIMULATION_SCHEMA.md`: `src/lib/types.ts`.
- Server state: TanStack Query (`useQuery`/`useMutation`).
- App/workstation state: Zustand store `src/store/workstation.ts`:
  - `activeView: ViewId`
  - `simId: string | null` — active simulation
  - `sim: SimulationResult | null` — cached result doc
  - `playback: { playing, speed (ticks/s), cursorTick }`
  - `filters: { tasks: string[], eventTypes: string[] }`
- Query keys: `['sim', id]`, `['experiments']`, `['simulations']`,
  `['comparison', id]`, `['reports', simId]`.

## Views (ViewId → file)

Views live in `src/components/views/`. The shell `src/app/page.tsx` renders
`WorkstationShell` with a view registry — **new views register in
`src/components/views/registry.tsx` only**.

| ViewId | file | purpose |
|---|---|---|
| workbench | `WorkbenchView.tsx` | experiment library, config editor (form + JSON), validation, run |
| timeline | `TimelineView.tsx` | Gantt timeline: lanes, states, zoom/pan, cursor, event inspect |
| live | `LiveView.tsx` | playback console: transport, tick counter, live event stream, task LEDs |
| trace | `TraceView.tsx` | full event table: filter, sort, search, CSV export |
| resources | `ResourcesView.tsx` | mutex/sem/msgq/evflags state at cursor + history |
| deadlock | `DeadlockView.tsx` | resource-allocation graph (SVG), cycle highlight, related events |
| memory | `MemoryView.tsx` | memory map over time, fragmentation stats, failures/leaks |
| metrics | `MetricsView.tsx` | per-task table + aggregate cards + recharts charts |
| compare | `CompareView.tsx` | scheduler comparison: multi-select, run-all, table + grouped bars |
| reports | `ReportsView.tsx` | report list, rendered report, JSON/CSV export, print |
| docs | `DocsView.tsx` | built-in quick guide + about/license screen |

## Design system — "logic-analyzer workstation" identity

- Dark-first, `next-themes` (default dark; light mode via CSS vars).
- Palette (CSS variables in `globals.css`, tailwind classes map to them):

| token | dark | light | use |
|---|---|---|---|
| `--bg` | #17140f (warm near-black) | #faf9f6 | page background |
| `--panel` | #201c15 | #ffffff | raised panels |
| `--line` | #3a332a | #e7e2d8 | hairline borders |
| `--ink` | #ede8dd | #26221a | primary text |
| `--ink-dim` | #8a8271 | #6f6a5d | secondary text |
| `--accent` (amber) | #f5b642 | #b45309 | RUNNING, primary actions |
| `--state-running` | #f5b642 | #d97706 | gantt running |
| `--state-ready` | #6b6353 @60% | #a8a29a | ready |
| `--state-blocked` | #e4574d | #be123c | resource wait |
| `--state-waiting` | #a3c949 | #4d7c0f | I/O wait |
| `--state-sleeping` | #45b3a4 | #0f766e | sleep |
| `--state-terminated` | #3a352c hatch | #d6d3cd hatch | done |
| `--ctx` | #57503f dashes | #a8a29a dashes | context switch |
| `--ok` | #7dbb72 | #15803d | success |
| `--bad` | #e4574d | #b91c1c | errors/deadlock |

- No blue/indigo. Amber = time/running; red = blocked/danger; green = I/O/ok;
  teal = sleep; stone = idle/ready.
- Typography: `font-mono` (ui-monospace stack) for all numeric readouts,
  ticks, metrics, trace table, tick axis. Sans for prose only.
- Layout: left 56px instrument rail (icon buttons w/ tooltips + view id
  number 01–11), persistent bottom **transport bar** (global: sim name,
  status LED, tick counter, play/pause, speed, cursor position) — the
  timeline views bind to the shared cursor. Top slim header with wordmark
  `MicroRT·Lab`, active scheduler badge, sim status.
- Signature interaction: timeline zoom with wheel (anchored at pointer),
  drag-pan, space = play/pause, ←/→ = step ±1 tick, number keys 1–9 switch views.
- Motion: framer-motion 150ms fade/slide between views; no bouncy springs.
- States to implement everywhere: initial, empty, loading (skeletons),
  running, completed, error (toast + inline), no-data.

## Component conventions

- `p-4`/`p-6` panel padding, `gap-4` grid gaps, 1px hairlines (`border-line`).
- Long lists: `max-h-96 overflow-y-auto` custom thin scrollbar.
- shadcn/ui for buttons/selects/tabs/dialogs/toasts; custom SVG for gantt,
  RAG graph, memory map (no chart lib for those); recharts for metrics bars.
- 44px touch targets; `sr-only` labels; semantic `main/header/nav`.
- Sticky footer: root wrapper `min-h-screen flex flex-col`, footer `mt-auto`
  (footer = transport bar zone; page never scrolls under it).

## Build/QA

- `bun run lint` clean; strict TS (`tsc --noEmit` via next build path);
  no `any` in committed code (use `unknown` + guards).
- Error boundary at shell level with retry.
- All fetches through `src/lib/api.ts` with timeouts + typed errors
  (`ApiError { status, message, details? }`).
