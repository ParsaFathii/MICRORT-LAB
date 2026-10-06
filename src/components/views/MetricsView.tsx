"use client";

/**
 * MicroRT-Lab — Metrics view (08): aggregate stat tiles (engine values with
 * Python-recomputed delta chips — the cross-check made visible), per-task
 * table, recharts (grouped bars / ready-queue line / CPU donut) and the RT
 * analysis section (Liu & Layland bound vs measured utilization).
 */

import * as React from "react";
import {
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  Legend,
  Line,
  LineChart,
  Pie,
  PieChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { GanttSeg, SimulationResult, TaskResult } from "@/lib/types";
import {
  useSimulationAnalysis,
  useSimulationMetricsCheck,
} from "@/hooks/useReports";
import { useWorkstation } from "@/store/workstation";
import {
  EmptyPanel,
  SectionPanel,
  type SimDoc,
  SimGate,
  StatTile,
  useTick,
  ViewHeader,
} from "@/components/views/shared";

/* ---------------------------------------------------------------------- */
/* helpers                                                                 */
/* ---------------------------------------------------------------------- */

const fmt = (n: number | null | undefined, digits = 3): string =>
  n === null || n === undefined || Number.isNaN(n)
    ? "—"
    : Math.abs(n) >= 1000
      ? n.toFixed(0)
      : n.toFixed(digits);

const mean = (arr: number[]): number | null =>
  arr.length === 0 ? null : arr.reduce((a, b) => a + b, 0) / arr.length;

function readyQueueSeries(gantt: GanttSeg[], horizon: number): { t: number; q: number }[] {
  const ticks = new Set<number>([0]);
  for (const seg of gantt) {
    ticks.add(seg.t0);
    ticks.add(seg.t1);
  }
  const sorted = [...ticks].filter((t) => t <= horizon).sort((a, b) => a - b);
  return sorted.map((t) => ({
    t,
    q: gantt.reduce((n, seg) => n + (seg.state === "READY" && t >= seg.t0 && t < seg.t1 ? 1 : 0), 0),
  }));
}

const CHART_AXIS = {
  tick: { fill: "var(--ink-dim)", fontSize: 9, fontFamily: "var(--font-mono)" },
  stroke: "var(--line)",
};
const CHART_TOOLTIP = {
  background: "var(--panel)",
  border: "1px solid var(--line)",
  borderRadius: 6,
  fontSize: 10,
  fontFamily: "var(--font-mono)",
  color: "var(--ink)",
};

/* delta chip: engine vs Python recompute -------------------------------- */

function DeltaChip({ delta, tolerance }: { delta: number | null; tolerance: number }) {
  if (delta === null || Number.isNaN(delta)) return null;
  const ok = Math.abs(delta) < tolerance;
  return (
    <span
      title="Python recompute − engine value"
      className={cn(
        "rounded-sm border px-1 py-px font-mono text-[9px] tabular-nums",
        ok ? "border-ok/40 bg-ok/10 text-ok" : "border-bad/40 bg-bad/10 text-bad",
      )}
    >
      {delta >= 0 ? "+" : ""}
      {delta.toFixed(3)}
    </span>
  );
}

/* LL bound bar ------------------------------------------------------------ */

function BoundBar({ bound, utilization }: { bound: number; utilization: number }) {
  const max = Math.max(1, utilization * 1.15, bound * 1.25);
  const w = (v: number) => `${Math.min(100, (v / max) * 100)}%`;
  const within = utilization <= bound;
  return (
    <div className="space-y-1.5" aria-label="Liu and Layland bound versus total utilization">
      <div className="relative h-6 rounded-sm border border-line bg-bg">
        <div
          className={cn("absolute inset-y-0 left-0 rounded-sm", within ? "bg-ok/30" : "bg-bad/30")}
          style={{ width: w(utilization) }}
        />
        <div
          className={cn("absolute inset-y-0 w-0.5", within ? "bg-ok" : "bg-ok")}
          style={{ left: w(bound) }}
          title={`bound ${bound}`}
        />
        <span className="absolute top-1/2 left-2 -translate-y-1/2 font-mono text-[10px] text-ink tabular-nums">
          U={fmt(utilization)}
        </span>
        <span className="absolute top-1/2 right-2 -translate-y-1/2 font-mono text-[10px] text-ink-dim tabular-nums">
          bound {fmt(bound)}
        </span>
      </div>
      <p className="font-mono text-[9px] text-ink-dim">
        total utilization {within ? "≤" : ">"} sufficient bound →{" "}
        <span className={within ? "text-ok" : "text-bad"}>
          {within ? "RM-schedulable by the sufficient test" : "no guarantee from this bound"}
        </span>
      </p>
    </div>
  );
}

/* cursor-reactive caption (kept in a leaf so charts don't re-render) ----- */

function QueueCursorNote({ series }: { series: { t: number; q: number }[] }) {
  const horizon = series.length > 0 ? series[series.length - 1].t : 0;
  const tick = useTick(horizon);
  const q = series.find((p) => p.t >= tick)?.q ?? 0;
  return (
    <p className="mt-1 text-center font-mono text-[9px] text-ink-dim">
      q at cursor t={tick}: {q}
    </p>
  );
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function MetricsView() {
  return <SimGate render={(doc: SimDoc) => <MetricsBody key={doc.simId} doc={doc} />} />;
}

function MetricsBody({ doc }: { doc: SimDoc }) {
  const result = doc.result;
  const horizon = result.simulatedUntil;
  const setActiveView = useWorkstation((s) => s.setActiveView);
  const setCursor = useWorkstation((s) => s.setCursor);

  const metrics = result.metrics;
  const simId = doc.simId;
  const check = useSimulationMetricsCheck(simId);
  const analysis = useSimulationAnalysis(simId);

  /* aggregate tiles: [label, engine value, cross-check metric key] */
  const tiles: { label: string; value: string; key?: string; tone?: "accent" | "bad" }[] = [
    { label: "avg waiting", value: fmt(metrics.avgWaiting), key: "avgWaiting" },
    { label: "avg turnaround", value: fmt(metrics.avgTurnaround), key: "avgTurnaround" },
    { label: "avg response", value: fmt(metrics.avgResponse), key: "avgResponse" },
    { label: "throughput", value: fmt(metrics.throughput), key: "throughput" },
    { label: "cpu util", value: fmt(metrics.cpuUtilization), key: "cpuUtilization", tone: "accent" },
    { label: "completion", value: fmt(metrics.completionRate) },
    { label: "ctx switches", value: String(metrics.ctxSwitches) },
    { label: "preemptions", value: String(metrics.preemptions) },
    {
      label: "deadline misses",
      value: String(metrics.deadlineMisses),
      tone: metrics.deadlineMisses > 0 ? "bad" : undefined,
    },
    { label: "max queue", value: String(metrics.maxQueueLen) },
    { label: "events", value: String(metrics.totalEvents) },
  ];

  const perTaskBars = React.useMemo(
    () =>
      result.tasks.map((t: TaskResult) => ({
        id: t.id,
        waiting: mean(t.waitTimes) ?? 0,
        turnaround: mean(t.turnaroundTimes) ?? 0,
        response: mean(t.responseTimes) ?? 0,
      })),
    [result.tasks],
  );

  const queueSeries = React.useMemo(
    () => readyQueueSeries(result.gantt, horizon),
    [result.gantt, horizon],
  );

  const busy = Math.max(0, horizon - metrics.idleTime);
  const donut = [
    { name: "busy", value: busy },
    { name: "idle", value: Math.max(0, metrics.idleTime) },
  ];

  const rt = analysis.data?.rt ?? null;
  const rtError = analysis.isError;
  const starved = analysis.data?.starvation?.starvedTasks ?? [];

  const seekTask = (id: string) => {
    const first = result.trace.find((e) => e.task === id);
    if (first) {
      setCursor(first.t);
      setActiveView("trace");
    }
  };

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="08" label="Metrics" hint="engine metrics + Python cross-check + RT analysis">
        <span className="font-mono text-[10px] text-ink-dim">
          cross-check {check.isError ? "unavailable" : check.data?.match ? "match" : "mismatch"}
        </span>
      </ViewHeader>

      <div className="min-h-0 flex-1 space-y-3 overflow-y-auto p-4">
        {/* aggregate tiles */}
        <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 2xl:grid-cols-6">
          {tiles.map((tile) => (
            <StatTile
              key={tile.label}
              label={tile.label}
              value={tile.value}
              tone={tile.tone ?? "ink"}
              sub={
                tile.key && check.data ? (
                  <DeltaChip
                    delta={check.data.deltas[tile.key] ?? null}
                    tolerance={check.data.tolerance}
                  />
                ) : undefined
              }
            />
          ))}
        </div>
        {check.isError && (
          <div className="flex items-center gap-3 rounded-md border border-line bg-panel px-3 py-2">
            <p className="font-mono text-[10px] text-ink-dim">
              metrics cross-check failed:{" "}
              {check.error instanceof Error ? check.error.message : "unknown error"}
            </p>
            <Button
              type="button"
              size="sm"
              variant="outline"
              className="ml-auto h-7 cursor-pointer rounded-md font-mono text-[10px] hover:bg-accent/10"
              onClick={() => void check.refetch()}
            >
              RETRY
            </Button>
          </div>
        )}

        {/* charts */}
        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <SectionPanel title="PER-TASK AVERAGES · WAIT / TURNAROUND / RESPONSE">
            <div className="h-56">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={perTaskBars} margin={{ top: 8, right: 8, bottom: 4, left: 0 }}>
                  <CartesianGrid stroke="var(--line)" strokeDasharray="2 4" />
                  <XAxis dataKey="id" tick={CHART_AXIS.tick} stroke={CHART_AXIS.stroke} tickLine={false} />
                  <YAxis tick={CHART_AXIS.tick} stroke={CHART_AXIS.stroke} tickLine={false} width={36} />
                  <Tooltip contentStyle={CHART_TOOLTIP} cursor={{ fill: "var(--accent)", fillOpacity: 0.08 }} />
                  <Legend wrapperStyle={{ fontSize: 9, fontFamily: "var(--font-mono)" }} />
                  <Bar dataKey="waiting" name="wait" fill="var(--accent)" radius={[3, 3, 0, 0]} isAnimationActive={false} />
                  <Bar dataKey="turnaround" name="turn" fill="var(--state-waiting)" radius={[3, 3, 0, 0]} isAnimationActive={false} />
                  <Bar dataKey="response" name="resp" fill="var(--state-sleeping)" radius={[3, 3, 0, 0]} isAnimationActive={false} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </SectionPanel>

          <SectionPanel title="READY-QUEUE LENGTH OVER TIME">
            <div className="h-56">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={queueSeries} margin={{ top: 8, right: 8, bottom: 4, left: 0 }}>
                  <CartesianGrid stroke="var(--line)" strokeDasharray="2 4" />
                  <XAxis dataKey="t" type="number" domain={["dataMin", "dataMax"]} tick={CHART_AXIS.tick} stroke={CHART_AXIS.stroke} tickLine={false} />
                  <YAxis allowDecimals={false} tick={CHART_AXIS.tick} stroke={CHART_AXIS.stroke} tickLine={false} width={36} />
                  <Tooltip contentStyle={CHART_TOOLTIP} labelFormatter={(t) => `t=${String(t)}`} />
                  <Line
                    type="stepAfter"
                    dataKey="q"
                    name="ready tasks"
                    stroke="var(--accent)"
                    strokeWidth={1.5}
                    dot={false}
                    isAnimationActive={false}
                  />
                </LineChart>
              </ResponsiveContainer>
              <QueueCursorNote series={queueSeries} />
            </div>
          </SectionPanel>
        </div>

        {/* per-task table + donut */}
        <div className="grid grid-cols-1 gap-3 xl:grid-cols-[1fr_320px]">
          <SectionPanel title="PER-TASK METRICS · CLICK A ROW TO OPEN ITS FIRST EVENT">
            <div className="overflow-x-auto">
              <table className="w-full border-collapse font-mono text-[11px]">
                <thead>
                  <tr className="text-left">
                    {[
                      "ID",
                      "KIND",
                      "P",
                      "JOBS R/C",
                      "CPU",
                      "READY WAIT",
                      "BLOCKED",
                      "WAIT/SLEEP",
                      "AVG WAIT",
                      "AVG TURN",
                      "MISS",
                      "STARVED",
                    ].map((h) => (
                      <th
                        key={h}
                        className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim"
                      >
                        {h}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {result.tasks.map((t) => (
                    <tr
                      key={t.id}
                      tabIndex={0}
                      onClick={() => seekTask(t.id)}
                      onKeyDown={(e) => {
                        if (e.key === "Enter") seekTask(t.id);
                      }}
                      className={cn(
                        "cursor-pointer border-b border-line/40 hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-accent",
                        t.starved && "bg-bad/5",
                      )}
                      aria-label={`Task ${t.id}`}
                    >
                      <td className="px-2 py-1 text-ink">{t.id}</td>
                      <td className="px-2 py-1 text-ink-dim">{t.kind}</td>
                      <td className="px-2 py-1 text-ink tabular-nums">{t.priority}</td>
                      <td className="px-2 py-1 text-ink tabular-nums">
                        {t.jobsReleased}/{t.jobsCompleted}
                      </td>
                      <td className="px-2 py-1 text-right text-ink tabular-nums">{t.cpuTime}</td>
                      <td className="px-2 py-1 text-right text-ink tabular-nums">{t.readyWaitTotal}</td>
                      <td className="px-2 py-1 text-right text-ink tabular-nums">{t.blockedTotal}</td>
                      <td className="px-2 py-1 text-right text-ink-dim tabular-nums">
                        {t.waitingTotal}/{t.sleepingTotal}
                      </td>
                      <td className="px-2 py-1 text-right text-ink tabular-nums">{fmt(mean(t.waitTimes))}</td>
                      <td className="px-2 py-1 text-right text-ink tabular-nums">
                        {fmt(mean(t.turnaroundTimes))}
                      </td>
                      <td
                        className={cn(
                          "px-2 py-1 text-right tabular-nums",
                          t.deadlineMisses > 0 ? "text-bad" : "text-ink-dim",
                        )}
                      >
                        {t.deadlineMisses}
                      </td>
                      <td className="px-2 py-1 text-center">
                        {t.starved ? (
                          <span className="rounded-sm border border-bad/40 bg-bad/10 px-1.5 py-px text-[9px] text-bad">
                            STARVED
                          </span>
                        ) : (
                          <span className="text-ink-dim">·</span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </SectionPanel>

          <SectionPanel title="CPU UTILIZATION (MEASURED)">
            <div className="h-40">
              <ResponsiveContainer width="100%" height="100%">
                <PieChart>
                  <Pie
                    data={donut}
                    dataKey="value"
                    nameKey="name"
                    innerRadius="62%"
                    outerRadius="88%"
                    strokeWidth={1}
                    isAnimationActive={false}
                  >
                    <Cell fill="var(--accent)" />
                    <Cell fill="var(--state-ready)" />
                  </Pie>
                  <Tooltip contentStyle={CHART_TOOLTIP} />
                </PieChart>
              </ResponsiveContainer>
            </div>
            <div className="flex items-center justify-center gap-4 font-mono text-[10px]">
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-2 rounded-full bg-accent" aria-hidden />
                busy {busy}t
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-2 rounded-full bg-state-ready" aria-hidden />
                idle {metrics.idleTime}t
              </span>
              <span className="text-ink-dim tabular-nums">{fmt(metrics.cpuUtilization)}</span>
            </div>
            {starved.length > 0 && (
              <p className="mt-3 rounded-md border border-bad/30 bg-bad/5 px-2 py-1.5 font-mono text-[10px] text-bad">
                STARVED: {starved.join(", ")}
              </p>
            )}
          </SectionPanel>
        </div>

        {/* RT analysis */}
        <SectionPanel
          title="RATE-MONOTONIC ANALYSIS · THEORETICAL BOUND VS MEASURED (SIMULATION)"
          right={
            rtError ? (
              <Button
                type="button"
                size="sm"
                variant="outline"
                className="h-7 cursor-pointer rounded-md font-mono text-[10px] hover:bg-accent/10"
                onClick={() => void analysis.refetch()}
              >
                RETRY ANALYSIS
              </Button>
            ) : undefined
          }
        >
          {analysis.isLoading && (
            <p className="py-2 font-mono text-[10px] text-ink-dim">loading analysis…</p>
          )}
          {rtError && (
            <p className="py-2 font-mono text-[10px] text-bad">
              analysis failed:{" "}
              {analysis.error instanceof Error ? analysis.error.message : "unknown error"}
            </p>
          )}
          {rt && (
            <div className="grid grid-cols-1 gap-4 xl:grid-cols-[1fr_360px]">
              <div className="overflow-x-auto">
                <table className="w-full border-collapse font-mono text-[11px]">
                  <thead>
                    <tr className="text-left">
                      {["TASK", "WCET", "PERIOD", "DEADLINE", "UTILIZATION"].map((h) => (
                        <th
                          key={h}
                          className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim"
                        >
                          {h}
                        </th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {rt.theoretical.tasks.map((t) => (
                      <tr key={t.id} className="border-b border-line/40">
                        <td className="px-2 py-1 text-ink">{t.id}</td>
                        <td className="px-2 py-1 text-right text-ink tabular-nums">{t.wcet}</td>
                        <td className="px-2 py-1 text-right text-ink tabular-nums">{t.period}</td>
                        <td className="px-2 py-1 text-right text-ink-dim tabular-nums">
                          {t.relativeDeadline ?? "—"}
                        </td>
                        <td className="px-2 py-1 text-right text-accent tabular-nums">
                          {fmt(t.utilization)}
                        </td>
                      </tr>
                    ))}
                    <tr>
                      <td className="px-2 py-1 text-ink-dim" colSpan={4}>
                        total utilization ({rt.theoretical.periodicTaskCount} periodic tasks)
                      </td>
                      <td className="px-2 py-1 text-right font-bold text-accent tabular-nums">
                        {fmt(rt.theoretical.totalUtilization)}
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
              <div className="space-y-3">
                <div>
                  <p className="mb-1 font-mono text-[9px] tracking-widest text-ink-dim">
                    LIU & LAYLAND BOUND (THEORETICAL, SUFFICIENT)
                  </p>
                  <BoundBar
                    bound={rt.theoretical.liuLaylandBound}
                    utilization={rt.theoretical.totalUtilization}
                  />
                </div>
                <div className="grid grid-cols-2 gap-2">
                  <StatTile
                    label="measured misses"
                    value={String(rt.measured.deadlineMisses)}
                    tone={rt.measured.deadlineMisses > 0 ? "bad" : "ok"}
                  />
                  <StatTile
                    label="measured cpu util"
                    value={fmt(rt.measured.cpuUtilization)}
                    sub={`scheduler ${rt.measured.scheduler}`}
                  />
                </div>
                {rt.theoretical.hyperbolicBound && (
                  <p className="font-mono text-[10px] text-ink-dim">
                    hyperbolic bound Π(Ui+1) = {fmt(rt.theoretical.hyperbolicBound.product)} vs limit{" "}
                    {fmt(rt.theoretical.hyperbolicBound.limit)} →{" "}
                    <span
                      className={rt.theoretical.hyperbolicBound.schedulable ? "text-ok" : "text-bad"}
                    >
                      {rt.theoretical.hyperbolicBound.schedulable ? "schedulable" : "no guarantee"}
                    </span>
                  </p>
                )}
                {rt.verdict && (
                  <p className="rounded-md border border-line bg-bg px-2 py-1.5 font-mono text-[10px] text-ink-dim">
                    {rt.verdict}
                  </p>
                )}
                {rt.theoretical.note && (
                  <p className="font-mono text-[9px] leading-4 text-ink-dim/80">
                    {rt.theoretical.note}
                  </p>
                )}
              </div>
            </div>
          )}
          {!analysis.isLoading && !rtError && !rt && (
            <EmptyPanel label="NO RT ANALYSIS" hint="No periodic tasks in this simulation." />
          )}
        </SectionPanel>
      </div>
    </div>
  );
}
