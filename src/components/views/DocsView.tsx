"use client";

/**
 * MicroRT-Lab — Docs view (11): built-in guide + about (fully static, no
 * fetches). Guide = concise workstation walkthrough; About = project identity,
 * architecture layer diagram, license and repository.
 */

import * as React from "react";
import { ExternalLink } from "lucide-react";

import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import { SectionPanel, ViewHeader } from "@/components/views/shared";

/* ---------------------------------------------------------------------- */
/* guide                                                                   */
/* ---------------------------------------------------------------------- */

const GUIDE: { num: string; title: string; body: string[] }[] = [
  {
    num: "01",
    title: "Run an experiment",
    body: [
      "Open Workbench (key 1), pick an experiment from the library and press RUN EXPERIMENT.",
      "The engine runs deterministically; the result document lands in the cache and the timeline opens automatically.",
      "The CLONE panel lets you edit the config JSON and run it as a new simulation.",
    ],
  },
  {
    num: "02",
    title: "Read the timeline",
    body: [
      "Lanes are tasks, the x-axis is ticks. Colors: amber = RUNNING, grey = READY, red = BLOCKED, green = I/O WAIT, teal = SLEEPING, hatched = TERMINATED, dashed = context switch.",
      "Scroll-wheel to zoom (anchored at the pointer), drag to pan, click anywhere to seek. Click a segment or a marker (◆ preempt, ▼ deadline miss, ● complete) to inspect its events.",
      "The amber vertical line is the shared playback cursor.",
    ],
  },
  {
    num: "03",
    title: "Scrub and play",
    body: [
      "The transport bar is global: space toggles play/pause, ←/→ step one tick, the speed selector scales playback.",
      "Every view reacts to the cursor — Trace highlights the row at t, Resources/Deadlock/Memory re-derive their state, Live streams the console.",
    ],
  },
  {
    num: "04",
    title: "Trace and filters",
    body: [
      "Trace (key 4) is the dense event browser: type filter with counts, task chips (shared with the timeline), free-text search and a cursor-≤ filter.",
      "Click a row for the full JSON record, export the filtered rows as CSV, or let the view follow the cursor while playing.",
    ],
  },
  {
    num: "05",
    title: "Resources at the cursor",
    body: [
      "Resources (key 5) replays each mutex/semaphore/message-queue/event-flag's events up to the cursor: current owner, sem count, buffered messages, flag bits.",
      "The red markers on the mini-strip are contention events — click the strip to jump.",
    ],
  },
  {
    num: "06",
    title: "Read the RAG graph",
    body: [
      "Deadlock (key 6) draws the resource-allocation graph: dashed red edges are requests (task waits for a resource), solid amber edges are assignments (resource held by a task).",
      "A cycle in this graph is a deadlock — involved nodes and edges pulse red and the banner lists the chain A → M2 → B → M1 → A.",
      "Click a related event to jump straight to it in the Trace view.",
    ],
  },
  {
    num: "07",
    title: "Scrub memory",
    body: [
      "Memory (key 7) replays alloc/free events at the cursor — press play and watch allocations carve the address space; the hatched area is free.",
      "Click a block to inspect its owner, size and lifecycle events. The area chart tracks used bytes and fragmentation over time; failures and leaks get their own tables.",
    ],
  },
  {
    num: "08",
    title: "Metrics and the cross-check",
    body: [
      "Metrics (key 8) shows engine aggregates, the per-task table and charts (per-task averages, ready-queue length, CPU donut).",
      "The small ± chips on the tiles are the Python recompute deltas — green means engine and analysis layer agree within 0.001.",
      "The RT section compares the theoretical Liu & Layland bound with the measured utilization.",
    ],
  },
  {
    num: "09",
    title: "Compare schedulers",
    body: [
      "Compare (key 9) runs variants of one base config in one click: RR quantum 2/4/16, aging rescue, EDF vs fixed priority, first vs best fit.",
      "Build your own variants (scheduler, quantum, memory policy, aging), then read the metric × variant matrix — best values highlighted green, worst red — and export the markdown report.",
    ],
  },
  {
    num: "10",
    title: "Reports",
    body: [
      "Reports (key 0) fetches the analysis layer's markdown/CSV/JSON exports for the current simulation, renders them, and keeps every generated document in the library for download or print.",
    ],
  },
];

const SHORTCUTS: [string, string][] = [
  ["1 … 9, 0, -", "switch views (01–11)"],
  ["Space", "play / pause playback"],
  ["← / →", "step the cursor ±1 tick"],
  ["Wheel (timeline)", "zoom anchored at the pointer"],
  ["Drag (timeline)", "pan"],
  ["Click chart / strip / event", "seek cursor to that tick"],
];

function GuideTab() {
  return (
    <div className="grid grid-cols-1 gap-3 lg:grid-cols-2">
      {GUIDE.map((section) => (
        <section key={section.num} className="rounded-lg border border-line bg-panel p-4">
          <p className="font-mono text-[10px] tracking-widest text-accent">{section.num}</p>
          <h3 className="mt-0.5 font-mono text-sm text-ink">{section.title}</h3>
          <ul className="mt-2 space-y-1.5">
            {section.body.map((line, i) => (
              <li key={i} className="text-xs leading-5 text-ink-dim">
                {line}
              </li>
            ))}
          </ul>
        </section>
      ))}
      <section className="rounded-lg border border-line bg-panel p-4 lg:col-span-2">
        <h3 className="font-mono text-sm text-ink">Keyboard shortcuts</h3>
        <table className="mt-2 w-full border-collapse font-mono text-[11px]">
          <tbody>
            {SHORTCUTS.map(([keys, desc]) => (
              <tr key={keys} className="border-b border-line/40">
                <td className="py-1 pr-4">
                  <kbd className="rounded-sm border border-line bg-bg px-1.5 py-0.5 text-[10px] text-ink">
                    {keys}
                  </kbd>
                </td>
                <td className="py-1 text-ink-dim">{desc}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </div>
  );
}

/* ---------------------------------------------------------------------- */
/* about                                                                   */
/* ---------------------------------------------------------------------- */

const LAYERS: { title: string; sub: string; stroke: string }[] = [
  { title: "C kernel", sub: "kernel/ · C11 primitives: TCB, queues, sync, fixed memory", stroke: "var(--state-ready)" },
  { title: "C++20 engine", sub: "engine/ · discrete-event deterministic simulator, 8 schedulers", stroke: "var(--accent)" },
  { title: "Python analysis", sub: "services/api/ · FastAPI: experiments, metrics cross-check, RT analysis", stroke: "var(--state-sleeping)" },
  { title: "Web workstation", sub: "src/ · Next.js + React client visualizing trace data", stroke: "var(--state-waiting)" },
];

function ArchitectureDiagram() {
  const boxH = 52;
  const gap = 22;
  const height = LAYERS.length * boxH + (LAYERS.length - 1) * gap;
  return (
    <svg
      viewBox={`0 0 360 ${height}`}
      className="h-auto w-full max-w-md font-mono"
      role="img"
      aria-label="Architecture diagram: C kernel, C++ engine, Python analysis, web UI"
    >
      {LAYERS.map((layer, i) => {
        const y = i * (boxH + gap);
        return (
          <g key={layer.title}>
            <rect
              x="10"
              y={y}
              width="340"
              height={boxH}
              rx="8"
              fill="var(--bg)"
              stroke={layer.stroke}
              strokeWidth="1.5"
            />
            <text x="26" y={y + 21} className="fill-ink text-[12px]">
              {layer.title}
            </text>
            <text x="26" y={y + 38} className="fill-ink-dim text-[9px]">
              {layer.sub}
            </text>
            {i < LAYERS.length - 1 && (
              <g>
                <line
                  x1="180"
                  y1={y + boxH + 3}
                  x2="180"
                  y2={y + boxH + gap - 3}
                  stroke="var(--ink-dim)"
                  strokeWidth="1.2"
                />
                <polygon
                  points={`180,${y + boxH + gap - 2} 176,${y + boxH + gap - 10} 184,${y + boxH + gap - 10}`}
                  fill="var(--ink-dim)"
                />
              </g>
            )}
          </g>
        );
      })}
    </svg>
  );
}

function AboutTab() {
  return (
    <div className="grid grid-cols-1 gap-3 lg:grid-cols-2">
      <SectionPanel title="MICRORT·LAB — DETERMINISTIC REAL-TIME OS & SCHEDULING LABORATORY">
        <p className="text-xs leading-5 text-ink-dim">
          A layered teaching and analysis workstation: a C kernel, a deterministic
          C++20 discrete-event engine, a Python analysis layer and this web UI.
          The UI never computes scheduler results — it visualizes the engine's
          trace, and the Python layer independently recomputes every headline
          metric as a cross-check.
        </p>
        <div className="mt-3 flex flex-wrap gap-1.5">
          {["deterministic replay", "8 schedulers", "RAG deadlock detection", "priority inheritance", "aging analysis", "memory fragmentation", "RM/EDF theory"].map(
            (tag) => (
              <span
                key={tag}
                className="rounded-sm border border-line bg-bg px-1.5 py-0.5 font-mono text-[9px] text-ink-dim"
              >
                {tag}
              </span>
            ),
          )}
        </div>
        <p className="mt-4 font-mono text-[10px] text-ink-dim">
          Copyright © 2026 Parsa Fathi · Apache-2.0
        </p>
        <a
          href="https://github.com/ParsaFathii/MICRORT-LAB"
          target="_blank"
          rel="noopener noreferrer"
          className={cn(
            "mt-2 inline-flex items-center gap-1.5 rounded-md border border-line bg-bg px-2.5 py-1.5 font-mono text-[11px] text-accent",
            "transition-colors hover:border-accent/40 hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
          )}
          aria-label="GitHub repository (opens in a new tab)"
        >
          <ExternalLink className="h-3.5 w-3.5" aria-hidden />
          github.com/ParsaFathii/MICRORT-LAB
        </a>
      </SectionPanel>

      <SectionPanel title="ARCHITECTURE">
        <ArchitectureDiagram />
        <p className="mt-3 font-mono text-[9px] leading-4 text-ink-dim/80">
          Data flows downward as JSON documents over /api/v1 — config in, result
          (trace + gantt + metrics) out. Byte-determinism: identical configs
          replay identically (wallMicros aside).
        </p>
      </SectionPanel>
    </div>
  );
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function DocsView() {
  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="11" label="Docs" hint="built-in guide + about — no network needed" />
      <div className="min-h-0 flex-1 overflow-y-auto p-4">
        <Tabs defaultValue="guide">
          <TabsList className="h-9">
            <TabsTrigger value="guide" className="font-mono text-[11px]">
              GUIDE
            </TabsTrigger>
            <TabsTrigger value="about" className="font-mono text-[11px]">
              ABOUT
            </TabsTrigger>
          </TabsList>
          <TabsContent value="guide" className="mt-3">
            <GuideTab />
          </TabsContent>
          <TabsContent value="about" className="mt-3">
            <AboutTab />
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
