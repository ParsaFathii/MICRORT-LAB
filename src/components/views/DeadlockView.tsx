"use client";

/**
 * MicroRT-Lab — Deadlock view (06): resource-allocation graph (RAG).
 *
 * Task nodes (left column) and resource nodes (right column) laid out in
 * declaration order. Edges are derived from the trace at the playback cursor:
 * request/wait edges (task → resource, dashed, blocked color) and
 * assignment/hold edges (resource → task, solid accent). Detected cycles
 * (result.deadlocks) get a bad banner, pulse animation and thicker bad edges.
 */

import * as React from "react";

import { cn } from "@/lib/utils";
import type {
  DeadlockRec,
  ResourceResult,
  SimulationResult,
  TaskResult,
  TraceEvent,
} from "@/lib/types";
import { useWorkstation } from "@/store/workstation";
import { deriveResourceLive, eventsByResource } from "@/components/views/resources/resourceState";
import { EmptyPanel, type SimDoc, SimGate, useTick, ViewHeader } from "@/components/views/shared";

/** deadlock with the cycle decoded from engine indices to id names */
interface DecodedDeadlock {
  t: number;
  cycle: string[];
  tasks: string[];
  resources: string[];
}

/** decode result.deadlocks[].cycle (alternating taskIdx/resIdx) into id names */
function decodeDeadlockCycle(
  deadlocks: readonly DeadlockRec[],
  taskIds: readonly string[],
  resIds: readonly string[],
): DecodedDeadlock[] {
  return deadlocks.map((d) => ({
    t: d.t,
    tasks: d.tasks,
    resources: d.resources,
    cycle: d.cycle.map((idx, i) => {
      const id = i % 2 === 0 ? taskIds[idx] : resIds[idx];
      return id ?? `#${idx}`;
    }),
  }));
}

/* ---------------------------------------------------------------------- */
/* graph model                                                             */
/* ---------------------------------------------------------------------- */

interface RagEdge {
  key: string;
  from: { x: number; y: number };
  to: { x: number; y: number };
  kind: "request" | "assign";
  inCycle: boolean;
}

interface RagNode {
  key: string;
  kind: "task" | "res";
  id: string;
  sub: string;
  x: number;
  y: number;
  inCycle: boolean;
  shape: "rect" | "diamond" | "square";
}

const TASK_W = 124;
const TASK_H = 44;
const RES_HALF = 26;
const COL_TASK_X = 92;
const COL_RES_X = 548;
const TOP = 56;
const ROW = 76;
const LEFT_MARGIN = 14;

function buildGraph(
  tasks: TaskResult[],
  resources: ResourceResult[],
  edgesAtCursor: Map<string, { owner: string | null; waiters: string[] }>,
  deadlocks: readonly DecodedDeadlock[],
): { nodes: RagNode[]; edges: RagEdge[]; height: number } {
  const cycleNodes = new Set<string>();
  const cycleEdgeKeys = new Set<string>();
  const taskIds = new Set(tasks.map((t) => t.id));
  const resIds = new Set(resources.map((r) => r.id));
  for (const d of deadlocks) {
    for (const id of [...d.tasks, ...d.resources]) cycleNodes.add(id);
    const n = d.cycle.length;
    for (let i = 0; i < n; i++) {
      const a = d.cycle[i];
      const b = d.cycle[(i + 1) % n];
      if (a === b) continue;
      if (taskIds.has(a) && resIds.has(b)) cycleEdgeKeys.add(`req:${a}>${b}`);
      else if (resIds.has(a) && taskIds.has(b)) cycleEdgeKeys.add(`hold:${a}>${b}`);
    }
  }

  const rows = Math.max(tasks.length, resources.length, 1);
  const height = TOP * 2 + (rows - 1) * ROW + TASK_H;
  const taskY = new Map<string, number>();
  const resY = new Map<string, number>();

  const nodes: RagNode[] = tasks.map((t, i) => {
    const y = TOP + i * ROW;
    taskY.set(t.id, y);
    return {
      key: `task:${t.id}`,
      kind: "task",
      id: t.id,
      sub: `p=${t.priority}`,
      x: COL_TASK_X,
      y,
      inCycle: cycleNodes.has(t.id),
      shape: "rect",
    };
  });
  resources.forEach((r, i) => {
    const y = TOP + i * ROW;
    resY.set(r.id, y);
    nodes.push({
      key: `res:${r.id}`,
      kind: "res",
      id: r.id,
      sub: r.type,
      x: COL_RES_X,
      y,
      inCycle: cycleNodes.has(r.id),
      shape: r.type === "mutex" ? "diamond" : "square",
    });
  });

  const edges: RagEdge[] = [];
  const pushEdge = (key: string, kind: "request" | "assign", fromId: string, toId: string) => {
    const isTaskFrom = kind === "request";
    const fromY = isTaskFrom ? taskY.get(fromId) : resY.get(fromId);
    const toY = isTaskFrom ? resY.get(toId) : taskY.get(toId);
    if (fromY === undefined || toY === undefined) return;
    const fromX = isTaskFrom ? COL_TASK_X + TASK_W / 2 : COL_RES_X - RES_HALF;
    const toX = isTaskFrom ? COL_RES_X - RES_HALF : COL_TASK_X + TASK_W / 2;
    edges.push({
      key,
      from: { x: fromX, y: fromY },
      to: { x: toX, y: toY },
      kind,
      inCycle: cycleEdgeKeys.has(key),
    });
  };

  // live edges at the cursor: request (waiters) + assignment (owner)
  for (const r of resources) {
    const state = edgesAtCursor.get(r.id);
    if (!state) continue;
    if (state.owner !== null) pushEdge(`hold:${r.id}>${state.owner}`, "assign", r.id, state.owner);
    for (const w of state.waiters) pushEdge(`req:${w}>${r.id}`, "request", w, r.id);
  }
  // cycle edges that are not (yet) live at the cursor are still drawn (bad)
  for (const key of cycleEdgeKeys) {
    if (edges.some((e) => e.key === key)) continue;
    const isReq = key.startsWith("req:");
    const body = isReq ? key.slice(4) : key.slice(5);
    const [fromId, toId] = body.split(">");
    if (fromId && toId) pushEdge(key, isReq ? "request" : "assign", fromId, toId);
  }

  return { nodes, edges, height };
}

/* ---------------------------------------------------------------------- */
/* SVG rendering                                                           */
/* ---------------------------------------------------------------------- */

function edgePath(edge: RagEdge): string {
  const { from, to } = edge;
  const mx = (from.x + to.x) / 2;
  const bend = edge.kind === "request" ? -14 : 14;
  const c1x = mx;
  const c1y = from.y + bend;
  const c2x = mx;
  const c2y = to.y - bend;
  return `M ${from.x} ${from.y} C ${c1x} ${c1y}, ${c2x} ${c2y}, ${to.x} ${to.y}`;
}

function RagSvg({
  nodes,
  edges,
  height,
  selectedKey,
  onSelectNode,
}: {
  nodes: RagNode[];
  edges: RagEdge[];
  height: number;
  selectedKey: string | null;
  onSelectNode: (node: RagNode) => void;
}) {
  return (
    <svg
      viewBox={`0 0 640 ${height}`}
      className="h-auto w-full min-w-[640px] font-mono"
      role="img"
      aria-label="Resource allocation graph"
    >
      <defs>
        <marker id="rag-arrow-request" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="var(--state-blocked)" />
        </marker>
        <marker id="rag-arrow-assign" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="var(--accent)" />
        </marker>
        <marker id="rag-arrow-cycle" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="var(--bad)" />
        </marker>
      </defs>

      {/* column captions */}
      <text x={COL_TASK_X} y={24} textAnchor="middle" className="fill-ink-dim text-[9px] tracking-widest">
        TASKS
      </text>
      <text x={COL_RES_X} y={24} textAnchor="middle" className="fill-ink-dim text-[9px] tracking-widest">
        RESOURCES
      </text>

      {/* edges */}
      {edges.map((edge) => {
        const cycle = edge.inCycle;
        const stroke = cycle
          ? "var(--bad)"
          : edge.kind === "request"
            ? "var(--state-blocked)"
            : "var(--accent)";
        return (
          <path
            key={edge.key}
            d={edgePath(edge)}
            fill="none"
            stroke={stroke}
            strokeWidth={cycle ? 2.5 : edge.kind === "assign" ? 1.8 : 1.5}
            strokeDasharray={edge.kind === "request" && !cycle ? "5 4" : undefined}
            markerEnd={cycle ? "url(#rag-arrow-cycle)" : edge.kind === "request" ? "url(#rag-arrow-request)" : "url(#rag-arrow-assign)"}
            opacity={cycle ? 1 : 0.85}
            className={cycle ? "animate-pulse" : undefined}
          >
            <title>
              {edge.kind === "request"
                ? `${edge.key.slice(4).split(">")[0]} waits for ${edge.key.slice(4).split(">")[1]}`
                : `${edge.key.slice(5).split(">")[0]} held by ${edge.key.slice(5).split(">")[1]}`}
            </title>
          </path>
        );
      })}

      {/* nodes */}
      {nodes.map((node) => {
        const selected = selectedKey === node.key;
        const cycle = node.inCycle;
        const common = {
          cursor: "pointer",
          onClick: () => onSelectNode(node),
          tabIndex: 0,
          onKeyDown: (e: React.KeyboardEvent) => {
            if (e.key === "Enter" || e.key === " ") {
              e.preventDefault();
              onSelectNode(node);
            }
          },
          "aria-label": `${node.kind === "task" ? "Task" : "Resource"} ${node.id}`,
          className: cycle ? "animate-pulse" : undefined,
        };
        const stroke = cycle ? "var(--bad)" : selected ? "var(--accent)" : "var(--line)";
        const strokeWidth = cycle ? 2.5 : selected ? 2 : 1.2;
        return (
          <g key={node.key} {...common}>
            {node.kind === "task" ? (
              <rect
                x={node.x - TASK_W / 2}
                y={node.y - TASK_H / 2}
                width={TASK_W}
                height={TASK_H}
                rx={8}
                fill="var(--panel)"
                stroke={stroke}
                strokeWidth={strokeWidth}
              />
            ) : node.shape === "diamond" ? (
              <polygon
                points={`${node.x},${node.y - RES_HALF} ${node.x + RES_HALF},${node.y} ${node.x},${node.y + RES_HALF} ${node.x - RES_HALF},${node.y}`}
                fill="var(--panel)"
                stroke={stroke}
                strokeWidth={strokeWidth}
              />
            ) : (
              <rect
                x={node.x - RES_HALF * 0.85}
                y={node.y - RES_HALF * 0.85}
                width={RES_HALF * 1.7}
                height={RES_HALF * 1.7}
                rx={4}
                fill="var(--panel)"
                stroke={stroke}
                strokeWidth={strokeWidth}
              />
            )}
            {/* label: id above/below the shape for resources, inside for tasks */}
            {node.kind === "task" ? (
              <>
                <text
                  x={node.x}
                  y={node.y - 2}
                  textAnchor="middle"
                  className="fill-ink text-[11px]"
                >
                  {node.id}
                </text>
                <text
                  x={node.x}
                  y={node.y + 12}
                  textAnchor="middle"
                  className="fill-ink-dim text-[9px]"
                >
                  {node.sub}
                </text>
              </>
            ) : (
              <>
                <text
                  x={node.x}
                  y={node.y - RES_HALF - 8}
                  textAnchor="middle"
                  className="fill-ink text-[11px]"
                >
                  {node.id}
                </text>
                <text
                  x={node.x}
                  y={node.y + RES_HALF + 12}
                  textAnchor="middle"
                  className="fill-ink-dim text-[9px]"
                >
                  {node.sub}
                </text>
              </>
            )}
          </g>
        );
      })}
    </svg>
  );
}

/* ---------------------------------------------------------------------- */
/* node info + related events                                              */
/* ---------------------------------------------------------------------- */

function NodeInfo({
  node,
  result,
  live,
}: {
  node: RagNode;
  result: SimulationResult;
  live: { owner: string | null; waiters: string[] } | null;
}) {
  if (node.kind === "task") {
    const t = result.tasks.find((x) => x.id === node.id);
    if (!t) return null;
    return (
      <dl className="grid grid-cols-2 gap-x-4 gap-y-1 font-mono text-[11px]">
        {[
          ["kind", t.kind],
          ["priority", String(t.priority)],
          ["jobs rel/comp", `${t.jobsReleased}/${t.jobsCompleted}`],
          ["deadline misses", String(t.deadlineMisses)],
          ["cpuTime", `${t.cpuTime}t`],
          ["blocked total", `${t.blockedTotal}t`],
          ["starved", t.starved ? "YES" : "no"],
          ["first/last", `${t.firstStart}…${t.lastFinish}`],
        ].map(([k, v]) => (
          <React.Fragment key={k}>
            <dt className="text-ink-dim">{k}</dt>
            <dd className={cn("text-right", v === "YES" && "text-bad")}>{v}</dd>
          </React.Fragment>
        ))}
      </dl>
    );
  }
  const r = result.resources.find((x) => x.id === node.id);
  if (!r) return null;
  return (
    <dl className="grid grid-cols-2 gap-x-4 gap-y-1 font-mono text-[11px]">
      {[
        ["type", r.type],
        ["acquisitions", String(r.acquisitions)],
        ["contentions", String(r.contentions)],
        ["queue depth", String(r.queueDepth)],
        ["final owner", r.finalOwner ?? r.holderAtEnd ?? "free"],
        ["owner at cursor", live?.owner ?? "free"],
        ["waiters at cursor", live && live.waiters.length > 0 ? live.waiters.join(", ") : "—"],
      ].map(([k, v]) => (
        <React.Fragment key={k}>
          <dt className="text-ink-dim">{k}</dt>
          <dd className="text-right text-ink">{v}</dd>
        </React.Fragment>
      ))}
    </dl>
  );
}

function RelatedEvents({
  events,
  onJump,
}: {
  events: TraceEvent[];
  onJump: (t: number) => void;
}) {
  return (
    <ul className="max-h-44 space-y-0.5 overflow-y-auto">
      {events.map((e) => (
        <li key={e.seq}>
          <button
            type="button"
            onClick={() => onJump(e.t)}
            aria-label={`Jump to trace at t=${e.t}, ${e.type}`}
            className="flex w-full cursor-pointer items-center gap-2 rounded-sm px-1.5 py-1 text-left font-mono text-[10px] hover:bg-accent/10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
          >
            <span className="text-accent tabular-nums">t={e.t}</span>
            <span className="rounded-sm border border-bad/40 bg-bad/10 px-1 py-px text-[9px] text-bad">
              {e.type}
            </span>
            <span className="text-ink">{e.task ?? "—"}</span>
            <span className="text-ink-dim">{e.res ?? ""}</span>
            <span className="ml-auto text-ink-dim/70">seq {e.seq}</span>
          </button>
        </li>
      ))}
    </ul>
  );
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function DeadlockView() {
  return <SimGate render={(doc: SimDoc) => <DeadlockBody key={doc.simId} result={doc.result} />} />;
}

function DeadlockBody({ result }: { result: SimulationResult }) {
  const horizon = result.simulatedUntil;
  const tick = useTick(horizon);
  const setCursor = useWorkstation((s) => s.setCursor);
  const pause = useWorkstation((s) => s.pause);
  const setActiveView = useWorkstation((s) => s.setActiveView);

  const [selected, setSelected] = React.useState<RagNode | null>(null);

  const decoded = React.useMemo(
    () =>
      decodeDeadlockCycle(
        result.deadlocks,
        result.config.tasks.map((t) => t.id),
        (result.config.resources ?? []).map((r) => r.id),
      ),
    [result.deadlocks, result.config.tasks, result.config.resources],
  );

  const eventsMap = React.useMemo(
    () => eventsByResource(result.trace, result.resources.map((r) => r.id)),
    [result.trace, result.resources],
  );

  /* live owner/waiters per resource at the cursor */
  const liveMap = React.useMemo(() => {
    const m = new Map<string, { owner: string | null; waiters: string[] }>();
    for (const r of result.resources) {
      const live = deriveResourceLive(eventsMap.get(r.id) ?? [], r.type, tick);
      m.set(r.id, {
        owner: live.kind === "mutex" ? live.owner : null,
        waiters:
          live.kind === "mutex"
            ? live.waiters
            : live.kind === "sem" || live.kind === "evflags"
              ? live.waiters
              : [...live.senders, ...live.receivers],
      });
    }
    return m;
  }, [eventsMap, result.resources, tick]);

  const graph = React.useMemo(
    () => buildGraph(result.tasks, result.resources, liveMap, decoded),
    [result.tasks, result.resources, liveMap, decoded],
  );

  const related = React.useMemo(() => {
    const involved = new Set(decoded.flatMap((d) => d.tasks));
    if (involved.size === 0) return [];
    return result.trace.filter(
      (e) => e.type === "DEADLOCK" || (e.type === "LOCK_BLOCK" && e.task !== null && involved.has(e.task)),
    );
  }, [result.trace, decoded]);

  const jump = (t: number) => {
    pause();
    setCursor(t);
    setActiveView("trace");
  };

  const hasDeadlock = decoded.length > 0;
  const noResources = result.resources.length === 0;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="06" label="Deadlock" hint="resource-allocation graph at the playback cursor" />

      {hasDeadlock && (
        <div className="shrink-0 space-y-2 border-b border-bad/30 bg-bad/5 px-4 py-3">
          <p className="font-mono text-[10px] tracking-widest text-bad">
            {decoded.length} CYCLE{decoded.length === 1 ? "" : "S"} DETECTED
          </p>
          {decoded.map((d, i) => (
            <div key={`${d.t}-${i}`} className="flex flex-wrap items-center gap-2">
              <span className="font-mono text-[10px] text-ink-dim">t={d.t}</span>
              <span className="flex flex-wrap items-center gap-1">
                {[...d.cycle, d.cycle[0]].map((id, j) => (
                  <React.Fragment key={`${id}-${j}`}>
                    {j > 0 && <span className="text-ink-dim">→</span>}
                    <span
                      className={cn(
                        "rounded-sm border px-1.5 py-0.5 font-mono text-[10px]",
                        result.tasks.some((t) => t.id === id)
                          ? "border-line bg-panel text-ink"
                          : "border-bad/40 bg-bad/10 text-bad",
                      )}
                    >
                      {id}
                    </span>
                  </React.Fragment>
                ))}
              </span>
            </div>
          ))}
        </div>
      )}

      <div className="min-h-0 flex-1 overflow-y-auto">
        <div className="p-4">
          {noResources ? (
            <EmptyPanel
              label="NO RESOURCES CONFIGURED"
              hint="A wait-for graph needs mutexes, semaphores, message queues or event flags."
            />
          ) : (
            <div className="grid grid-cols-1 gap-3 xl:grid-cols-[1fr_320px]">
              <section className="overflow-x-auto rounded-lg border border-line bg-panel">
                <header className="flex items-center gap-3 border-b border-line px-3 py-2">
                  <h3 className="font-mono text-[10px] tracking-widest text-ink-dim">
                    WAIT-FOR GRAPH
                  </h3>
                  <span className="ml-auto flex items-center gap-3 font-mono text-[9px] text-ink-dim">
                    <span className="flex items-center gap-1">
                      <svg width="20" height="6" aria-hidden>
                        <line x1="0" y1="3" x2="20" y2="3" stroke="var(--state-blocked)" strokeWidth="1.5" strokeDasharray="4 3" />
                      </svg>
                      request
                    </span>
                    <span className="flex items-center gap-1">
                      <svg width="20" height="6" aria-hidden>
                        <line x1="0" y1="3" x2="20" y2="3" stroke="var(--accent)" strokeWidth="1.8" />
                      </svg>
                      held
                    </span>
                    <span className="font-mono text-accent">at t={tick}</span>
                  </span>
                </header>
                <div style={{ marginLeft: LEFT_MARGIN }} className="p-3">
                  <RagSvg
                    nodes={graph.nodes}
                    edges={graph.edges}
                    height={graph.height}
                    selectedKey={selected?.key ?? null}
                    onSelectNode={setSelected}
                  />
                </div>
              </section>

              <div className="space-y-3">
                <section className="rounded-lg border border-line bg-panel">
                  <header className="border-b border-line px-3 py-2">
                    <h3 className="font-mono text-[10px] tracking-widest text-ink-dim">
                      NODE INSPECTOR
                    </h3>
                  </header>
                  <div className="p-3">
                    {selected ? (
                      <NodeInfo
                        node={selected}
                        result={result}
                        live={
                          selected.kind === "res" ? liveMap.get(selected.id) ?? null : null
                        }
                      />
                    ) : (
                      <p className="font-mono text-[10px] text-ink-dim">
                        Click a task or resource node for its statistics.
                      </p>
                    )}
                  </div>
                </section>

                {!hasDeadlock && (
                  <div className="flex min-h-20 items-center justify-center rounded-lg border border-dashed border-ok/30 bg-ok/5 p-4 text-center">
                    <div>
                      <p className="font-mono text-[10px] tracking-widest text-ok">
                        NO CYCLES DETECTED
                      </p>
                      <p className="mt-1 text-xs text-ink-dim">
                        The wait-for graph is acyclic — this simulation is deadlock-free.
                      </p>
                    </div>
                  </div>
                )}

                {hasDeadlock && related.length > 0 && (
                  <section className="rounded-lg border border-line bg-panel">
                    <header className="border-b border-line px-3 py-2">
                      <h3 className="font-mono text-[10px] tracking-widest text-ink-dim">
                        RELATED EVENTS · CLICK TO OPEN IN TRACE
                      </h3>
                    </header>
                    <div className="p-2">
                      <RelatedEvents events={related} onJump={jump} />
                    </div>
                  </section>
                )}
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
