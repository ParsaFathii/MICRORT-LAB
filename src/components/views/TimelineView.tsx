"use client";

/**
 * MicroRT-Lab — Timeline view (02): Gantt timeline + event inspector.
 * Playback controls live in the global transport bar; this view binds the
 * shared cursor to the chart.
 */

import * as React from "react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";
import type { GanttSeg, TaskState, TraceEvent } from "@/lib/types";
import { useSimulation } from "@/hooks/useSimulation";
import { useWorkstation } from "@/store/workstation";
import { NoSimPlaceholder } from "@/components/views/registry";
import {
  GanttTimeline,
  type TimelineSelection,
} from "@/components/timeline/GanttTimeline";
import { EventInspector } from "@/components/timeline/EventInspector";
import { TimelineLegend } from "@/components/timeline/TimelineLegend";
import { buildLanes, stateAtTick } from "@/components/timeline/ganttUtils";

const STATE_DOT: Record<TaskState, string> = {
  RUNNING: "bg-state-running",
  READY: "bg-state-ready",
  BLOCKED: "bg-state-blocked",
  WAITING: "bg-state-waiting",
  SLEEPING: "bg-state-sleeping",
  TERMINATED: "bg-state-terminated",
};

function ErrorPanel({ message, onRetry }: { message: string; onRetry: () => void }) {
  return (
    <div className="flex h-full items-center justify-center p-6">
      <div className="w-full max-w-md rounded-lg border border-bad/40 bg-panel p-6">
        <p className="font-mono text-[10px] tracking-widest text-bad">
          TIMELINE ERROR
        </p>
        <p className="mt-2 font-mono text-xs break-words text-ink-dim">{message}</p>
        <Button
          type="button"
          variant="outline"
          className="mt-4 cursor-pointer rounded-md hover:bg-accent/10"
          onClick={onRetry}
        >
          Retry
        </Button>
      </div>
    </div>
  );
}

export function TimelineView() {
  const simId = useWorkstation((s) => s.simId);
  const simQuery = useSimulation(simId);
  const cursorTick = useWorkstation((s) => s.playback.cursorTick);
  const playing = useWorkstation((s) => s.playback.playing);
  const hiddenTasks = useWorkstation((s) => s.filters.tasks);
  const toggleTaskFilter = useWorkstation((s) => s.toggleTaskFilter);
  const setCursor = useWorkstation((s) => s.setCursor);
  const pause = useWorkstation((s) => s.pause);

  const [selection, setSelection] = React.useState<TimelineSelection>(null);

  if (simId === null) return <NoSimPlaceholder />;
  if (simQuery.isLoading) {
    return (
      <div className="space-y-3 p-4">
        <Skeleton className="h-8 w-2/3" />
        <Skeleton className="h-[420px] w-full" />
      </div>
    );
  }
  if (simQuery.isError) {
    return (
      <ErrorPanel
        message={simQuery.error instanceof Error ? simQuery.error.message : "load failed"}
        onRetry={() => void simQuery.refetch()}
      />
    );
  }

  const record = simQuery.data;
  const result = record?.result ?? null;
  if (!result) {
    return (
      <div className="flex h-full items-center justify-center p-6">
        <div className="text-center">
          <p className="font-mono text-[10px] tracking-widest text-ink-dim">
            NO RESULT DOCUMENT
          </p>
          <p className="mt-2 text-sm text-ink-dim">
            This simulation has not been run yet — run it from the Workbench.
          </p>
        </div>
      </div>
    );
  }

  const horizon = result.simulatedUntil;
  const tick = Math.min(Math.floor(cursorTick), horizon);
  const lanes = buildLanes(result.gantt);
  const visibleTasks = result.tasks.filter((t) => !hiddenTasks.includes(t.id));

  const seek = (t: number) => {
    pause();
    setCursor(t);
  };

  const selectedKey =
    selection === null
      ? null
      : selection.kind === "segment"
        ? `${selection.seg.task}:${selection.seg.t0}:${selection.seg.t1}`
        : selection.events.length > 0
          ? `marker:${selection.events[0].seq}`
          : null;

  return (
    <div className="flex h-full min-h-0 flex-col">
      {/* toolbar: task chips + legend */}
      <div className="shrink-0 border-b border-line bg-panel px-3 py-2">
        <div className="flex flex-wrap items-center gap-1.5">
          <span className="mr-1 font-mono text-[10px] tracking-widest text-ink-dim">
            TASKS
          </span>
          {result.tasks.map((task) => {
            const hidden = hiddenTasks.includes(task.id);
            const state = stateAtTick(lanes.get(task.id), tick);
            return (
              <button
                key={task.id}
                type="button"
                onClick={() => toggleTaskFilter(task.id)}
                aria-pressed={!hidden}
                aria-label={`Toggle task ${task.id} lane visibility`}
                className={cn(
                  "flex cursor-pointer items-center gap-1.5 rounded-sm border px-2 py-1 font-mono text-[11px] transition-colors",
                  "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
                  hidden
                    ? "border-line bg-transparent text-ink-dim/50 line-through"
                    : "border-line bg-bg text-ink hover:border-accent/40",
                )}
              >
                <span
                  aria-hidden
                  className={cn(
                    "h-2 w-2 rounded-full",
                    state && !hidden ? STATE_DOT[state] : "bg-line",
                  )}
                />
                {task.id}
              </button>
            );
          })}
        </div>
        <TimelineLegend className="mt-2" />
      </div>

      {/* chart + inspector */}
      <div className="flex min-h-0 flex-1 flex-col lg:flex-row">
        <div className="relative min-h-[260px] flex-1 lg:min-h-0">
          <GanttTimeline
            key={simId}
            result={result}
            visibleTasks={visibleTasks}
            cursorTick={cursorTick}
            playing={playing}
            onSeek={seek}
            onScrub={(t) => setCursor(t)}
            onScrubStart={() => pause()}
            onSelectSegment={(seg: GanttSeg) => setSelection({ kind: "segment", seg })}
            onSelectMarker={(events: TraceEvent[]) => setSelection({ kind: "marker", events })}
            selectedKey={selectedKey}
          />
        </div>
        {selection !== null && (
          <EventInspector
            selection={selection}
            result={result}
            onClose={() => setSelection(null)}
            onSelectEvents={(events) => setSelection({ kind: "marker", events })}
            className="max-h-[45%] shrink-0 border-t lg:max-h-none lg:border-t-0"
          />
        )}
      </div>
    </div>
  );
}
