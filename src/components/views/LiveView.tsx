"use client";

/**
 * MicroRT-Lab — Live view (03): playback console.
 *
 * The rAF driver (src/hooks/usePlaybackClock.ts) is mounted globally by the
 * workstation shell — this view reads the shared cursor and derives the CPU
 * indicator, task LED board and event stream at each integer tick.
 */

import * as React from "react";

import { Cpu } from "lucide-react";

import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";
import type {
  GanttSeg,
  SimulationResult,
  TaskState,
  TraceEvent,
} from "@/lib/types";
import { useSimulation } from "@/hooks/useSimulation";
import { useWorkstation } from "@/store/workstation";
import { NoSimPlaceholder } from "@/components/views/registry";
import { CATEGORY_TEXT, eventCategory } from "@/components/timeline/eventColors";
import {
  buildLanes,
  runningAtTick,
  stateAtTick,
} from "@/components/timeline/ganttUtils";

const STREAM_WINDOW = 400;

const STATE_CHIP: Record<TaskState, string> = {
  RUNNING: "border-accent/50 bg-accent/15 text-accent",
  READY: "border-line bg-state-ready/15 text-ink-dim",
  BLOCKED: "border-state-blocked/50 bg-state-blocked/15 text-state-blocked",
  WAITING: "border-state-waiting/50 bg-state-waiting/15 text-state-waiting",
  SLEEPING: "border-state-sleeping/50 bg-state-sleeping/15 text-state-sleeping",
  TERMINATED: "border-line bg-state-terminated/25 text-ink-dim",
};

/** short `k=v` summary of a trace event's detail payload */
function detailSummary(event: TraceEvent): string {
  const entries = Object.entries(event.detail);
  if (entries.length === 0) return "";
  return entries
    .slice(0, 2)
    .map(([k, v]) => `${k}=${typeof v === "object" && v !== null ? JSON.stringify(v) : String(v)}`)
    .join(" ");
}

const ConsoleLine = React.memo(function ConsoleLine({
  event,
  tWidth,
  taskWidth,
  isCurrent,
  onSeek,
}: {
  event: TraceEvent;
  tWidth: number;
  taskWidth: number;
  isCurrent: boolean;
  onSeek: (t: number) => void;
}) {
  const category = eventCategory(event.type);
  return (
    <button
      type="button"
      onClick={() => onSeek(event.t)}
      className={cn(
        "flex w-full cursor-pointer items-baseline gap-2 px-2 py-px text-left font-mono text-[11px] leading-5 transition-colors",
        isCurrent ? "bg-accent/10" : "hover:bg-panel",
      )}
    >
      <span className="shrink-0 text-ink-dim">
        t={String(event.t).padStart(tWidth, "0")}
      </span>
      <span className={cn("shrink-0", CATEGORY_TEXT[category])}>
        {event.type.padEnd(16)}
      </span>
      <span className="shrink-0 text-ink">
        {(event.task ?? "").padEnd(taskWidth)}
      </span>
      <span className="truncate text-ink-dim">{detailSummary(event)}</span>
    </button>
  );
});

function BoardRow({
  taskId,
  state,
  lastEvent,
  running,
  taskWidth,
  onSeek,
}: {
  taskId: string;
  state: TaskState | null;
  lastEvent: TraceEvent | null;
  running: boolean;
  taskWidth: number;
  onSeek: (t: number) => void;
}) {
  return (
    <button
      type="button"
      onClick={() => onSeek(lastEvent ? lastEvent.t : 0)}
      className={cn(
        "flex w-full cursor-pointer items-center gap-2 border-l-2 px-2 py-1.5 text-left transition-colors hover:bg-accent/5",
        running ? "border-accent bg-accent/5" : "border-transparent",
      )}
      aria-label={`Task ${taskId} at cursor: ${state ?? "not started"}`}
    >
      <span
        className="shrink-0 font-mono text-xs text-ink"
        style={{ minWidth: `${taskWidth}ch` }}
      >
        {taskId}
      </span>
      <span
        className={cn(
          "w-[86px] shrink-0 rounded-sm border px-1.5 py-0.5 text-center font-mono text-[10px]",
          state ? STATE_CHIP[state] : "border-line bg-transparent text-ink-dim/50",
        )}
      >
        {state ?? "—"}
      </span>
      <span className="truncate font-mono text-[10px] text-ink-dim">
        {lastEvent
          ? `${lastEvent.type.toLowerCase()} ${detailSummary(lastEvent)}`
          : ""}
      </span>
    </button>
  );
}

/* ------------------------------------------------------------------------ */

export function LiveView() {
  const simId = useWorkstation((s) => s.simId);
  const simQuery = useSimulation(simId);

  if (simId === null) return <NoSimPlaceholder />;
  if (simQuery.isLoading) {
    return (
      <div className="space-y-3 p-4">
        <Skeleton className="h-16 w-full" />
        <Skeleton className="h-[420px] w-full" />
      </div>
    );
  }
  if (simQuery.isError) {
    return (
      <div className="flex h-full items-center justify-center p-6">
        <div className="rounded-lg border border-bad/40 bg-panel p-6">
          <p className="font-mono text-[10px] tracking-widest text-bad">LIVE ERROR</p>
          <p className="mt-2 font-mono text-xs text-ink-dim">
            {simQuery.error instanceof Error ? simQuery.error.message : "load failed"}
          </p>
        </div>
      </div>
    );
  }

  const result = simQuery.data?.result ?? null;
  if (!result) {
    return (
      <div className="flex h-full items-center justify-center p-6">
        <div className="text-center">
          <p className="font-mono text-[10px] tracking-widest text-ink-dim">
            NO RESULT DOCUMENT
          </p>
          <p className="mt-2 text-sm text-ink-dim">
            Run the simulation from the Workbench first.
          </p>
        </div>
      </div>
    );
  }
  return <LiveConsole result={result} />;
}

function LiveConsole({ result }: { result: SimulationResult }) {
  const cursorTick = useWorkstation((s) => s.playback.cursorTick);
  const speed = useWorkstation((s) => s.playback.speed);
  const setCursor = useWorkstation((s) => s.setCursor);
  const pause = useWorkstation((s) => s.pause);

  const consoleRef = React.useRef<HTMLDivElement | null>(null);
  const atBottomRef = React.useRef(true);

  const horizon = result.simulatedUntil;
  const tick = Math.min(Math.floor(cursorTick), horizon);
  const tWidth = Math.max(2, String(horizon).length);
  const taskWidth = Math.min(
    12,
    Math.max(4, ...result.tasks.map((t) => t.id.length)),
  );

  /* --- stable derived structures (recomputed only when result changes) --- */
  const lanes = React.useMemo(() => buildLanes(result.gantt), [result.gantt]);

  /** per-task trace indices, (t, seq)-sorted */
  const perTaskIdx = React.useMemo(() => {
    const map = new Map<string, number[]>();
    result.trace.forEach((ev, i) => {
      if (ev.task !== null) {
        const list = map.get(ev.task);
        if (list) list.push(i);
        else map.set(ev.task, [i]);
      }
    });
    return map;
  }, [result.trace]);

  const seek = React.useCallback(
    (t: number) => {
      pause();
      setCursor(t);
    },
    [pause, setCursor],
  );

  /* --- state at the cursor (recomputed per integer tick) --- */
  const running: { seg: GanttSeg; ctx: boolean } | null = runningAtTick(result.gantt, tick);
  const lastIdx = lastEventIndexAtTick(result.trace, tick);
  const rows = result.tasks.map((task) => {
    const idxList = perTaskIdx.get(task.id);
    let lastEvent: TraceEvent | null = null;
    if (idxList && idxList.length > 0) {
      const pos = lastIdxInList(idxList, tick, result.trace);
      if (pos >= 0) lastEvent = result.trace[idxList[pos]];
    }
    return {
      taskId: task.id,
      state: stateAtTick(lanes.get(task.id), tick),
      lastEvent,
      running: running?.seg.task === task.id,
    };
  });

  const streamStart = Math.max(0, lastIdx - STREAM_WINDOW + 1);
  const stream = lastIdx >= 0 ? result.trace.slice(streamStart, lastIdx + 1) : [];

  return (
    <div className="flex h-full min-h-0 flex-col">
      {/* top strip: CPU + tick counter */}
      <div className="flex shrink-0 items-center gap-4 border-b border-line bg-panel px-4 py-3">
        <div className="flex items-center gap-2">
          <Cpu className="h-4 w-4 text-ink-dim" aria-hidden />
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">CPU0</span>
          <span
            className={cn(
              "rounded-sm border px-2 py-0.5 font-mono text-xs",
              running
                ? running.ctx
                  ? "border-ctx/60 bg-ctx/15 text-ink"
                  : "border-accent/50 bg-accent/15 text-accent"
                : "border-line bg-transparent text-ink-dim",
            )}
          >
            {running ? (running.ctx ? "CTX" : running.seg.task) : "IDLE"}
          </span>
          {running && !running.ctx && (
            <span aria-hidden className="h-2 w-2 animate-pulse rounded-full bg-accent" />
          )}
        </div>

        <div className="font-mono">
          <span className="text-2xl leading-none text-accent">
            t={String(tick).padStart(tWidth, "0")}
          </span>
          <span className="ml-1 text-xs text-ink-dim">/{horizon}</span>
        </div>

        <div className="ml-auto flex items-center gap-4 font-mono text-[10px] tracking-widest text-ink-dim">
          <span>
            EV <span className="text-ink">{lastIdx + 1}</span>/{result.trace.length}
          </span>
          <span>
            <span className="text-ink">{speed}</span> t/s
          </span>
        </div>
      </div>

      {/* LED board + event console */}
      <div className="grid min-h-0 flex-1 grid-cols-1 lg:grid-cols-[minmax(280px,340px)_1fr]">
        <div className="min-h-0 overflow-y-auto border-b border-line lg:border-b-0 lg:border-r">
          <p className="sticky top-0 z-10 border-b border-line bg-panel px-2 py-1 font-mono text-[10px] tracking-widest text-ink-dim">
            TASK BOARD
          </p>
          {rows.map((row) => (
            <BoardRow
              key={row.taskId}
              taskId={row.taskId}
              state={row.state}
              lastEvent={row.lastEvent}
              running={row.running}
              taskWidth={taskWidth}
              onSeek={seek}
            />
          ))}
        </div>

        <div
          ref={consoleRef}
          className="min-h-0 overflow-y-auto bg-bg"
          onScroll={(e) => {
            const el = e.currentTarget;
            atBottomRef.current = el.scrollTop + el.clientHeight >= el.scrollHeight - 48;
          }}
          aria-label="Live event stream"
        >
          <p className="sticky top-0 z-10 border-b border-line bg-panel px-2 py-1 font-mono text-[10px] tracking-widest text-ink-dim">
            EVENT STREAM · last {STREAM_WINDOW}
          </p>
          {stream.length === 0 ? (
            <p className="px-2 py-2 font-mono text-[11px] text-ink-dim">
              — no events at t≤{tick} —
            </p>
          ) : (
            stream.map((event, i) => (
              <ConsoleLine
                key={event.seq}
                event={event}
                tWidth={tWidth}
                taskWidth={taskWidth}
                isCurrent={i === stream.length - 1}
                onSeek={seek}
              />
            ))
          )}
        </div>
      </div>

      {/* auto-scroll while pinned to the bottom; user scroll-up detaches */}
      <StreamAutoScroll
        count={stream.length}
        atBottomRef={atBottomRef}
        containerRef={consoleRef}
      />
    </div>
  );
}

/** last index (into a per-task index list) whose event t <= tick */
function lastIdxInList(
  idxList: number[],
  tick: number,
  trace: readonly TraceEvent[],
): number {
  let lo = 0;
  let hi = idxList.length - 1;
  let res = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (trace[idxList[mid]].t <= tick) {
      res = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return res;
}

/** last trace index with t <= tick (−1 when none) — binary search */
function lastEventIndexAtTick(trace: readonly { t: number }[], t: number): number {
  let lo = 0;
  let hi = trace.length - 1;
  let res = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (trace[mid].t <= t) {
      res = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return res;
}

/** Imperative auto-scroll that avoids re-running on every cursor frame. */
function StreamAutoScroll({
  count,
  atBottomRef,
  containerRef,
}: {
  count: number;
  atBottomRef: React.RefObject<boolean>;
  containerRef: React.RefObject<HTMLDivElement | null>;
}) {
  React.useEffect(() => {
    const el = containerRef.current;
    if (el && atBottomRef.current) {
      el.scrollTop = el.scrollHeight;
    }
  }, [count, atBottomRef, containerRef]);
  return null;
}
