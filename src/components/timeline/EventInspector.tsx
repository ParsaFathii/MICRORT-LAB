"use client";

/**
 * MicroRT-Lab — timeline event inspector (right side panel).
 * Shows either the events behind a clicked marker, or the summary of a
 * clicked gantt segment plus that task's events inside [t0, t1].
 */

import * as React from "react";

import { X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { GanttSeg, TaskResult, TraceEvent } from "@/lib/types";
import { CATEGORY_CHIP, CATEGORY_TEXT, eventCategory } from "./eventColors";
import type { TimelineSelection } from "./GanttTimeline";

interface EventInspectorProps {
  selection: Exclude<TimelineSelection, null>;
  result: {
    trace: TraceEvent[];
    tasks: TaskResult[];
  };
  onClose: () => void;
  /** clicking an event row inside a segment summary re-targets the inspector */
  onSelectEvents: (events: TraceEvent[]) => void;
  className?: string;
}

/* dim-syntax JSON renderer -------------------------------------------- */

function JsonBlock({ value }: { value: Record<string, unknown> }) {
  const entries = Object.entries(value);
  if (entries.length === 0) {
    return <p className="font-mono text-[10px] text-ink-dim">—</p>;
  }
  return (
    <div className="rounded-sm border border-line bg-bg p-2">
      <dl className="space-y-0.5">
        {entries.map(([k, v]) => (
          <div key={k} className="flex gap-2 font-mono text-[10px] leading-4">
            <dt className="shrink-0 text-accent">{k}:</dt>
            <dd className="break-all text-ink-dim">
              {typeof v === "object" && v !== null ? JSON.stringify(v) : String(v)}
            </dd>
          </div>
        ))}
      </dl>
    </div>
  );
}

/* event card ----------------------------------------------------------- */

function EventCard({ event }: { event: TraceEvent }) {
  const category = eventCategory(event.type);
  return (
    <div className="rounded-md border border-line bg-bg p-2.5">
      <div className="flex items-center gap-2">
        <span
          className={cn(
            "rounded-sm border px-1.5 py-0.5 font-mono text-[10px] tracking-wide",
            CATEGORY_CHIP[category],
          )}
        >
          {event.type}
        </span>
        <span className="ml-auto font-mono text-[10px] text-ink-dim">
          seq {event.seq}
        </span>
      </div>

      <dl className="mt-2 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 font-mono text-[11px]">
        <dt className="text-ink-dim">t</dt>
        <dd className="text-ink">{event.t}</dd>
        {event.task !== null && (
          <>
            <dt className="text-ink-dim">task</dt>
            <dd className="text-ink">{event.task}</dd>
          </>
        )}
        {event.res !== null && (
          <>
            <dt className="text-ink-dim">res</dt>
            <dd className="text-ink">{event.res}</dd>
          </>
        )}
        {(event.from !== null || event.to !== null) && (
          <>
            <dt className="text-ink-dim">state</dt>
            <dd className="text-ink">
              {event.from ?? "—"} <span className="text-ink-dim">→</span>{" "}
              {event.to ?? "—"}
            </dd>
          </>
        )}
        {event.dur !== null && (
          <>
            <dt className="text-ink-dim">dur</dt>
            <dd className="text-ink">{event.dur}t</dd>
          </>
        )}
      </dl>

      <div className="mt-2">
        <p className="mb-1 font-mono text-[9px] tracking-widest text-ink-dim">
          DETAIL
        </p>
        <JsonBlock value={event.detail} />
      </div>
    </div>
  );
}

/* segment summary ------------------------------------------------------- */

const STATE_CHIP: Record<string, string> = {
  RUNNING: "border-accent/40 bg-accent/10 text-accent",
  READY: "border-line bg-state-ready/15 text-ink-dim",
  BLOCKED: "border-state-blocked/40 bg-state-blocked/10 text-state-blocked",
  WAITING: "border-state-waiting/40 bg-state-waiting/10 text-state-waiting",
  SLEEPING: "border-state-sleeping/40 bg-state-sleeping/10 text-state-sleeping",
  TERMINATED: "border-line bg-state-terminated/20 text-ink-dim",
};

function SegmentSummary({ seg, task }: { seg: GanttSeg; task: TaskResult | undefined }) {
  return (
    <div className="rounded-md border border-line bg-bg p-2.5">
      <div className="flex items-center gap-2">
        <span className="font-mono text-xs text-ink">{seg.task}</span>
        <span
          className={cn(
            "rounded-sm border px-1.5 py-0.5 font-mono text-[10px]",
            STATE_CHIP[seg.state] ?? "border-line text-ink-dim",
          )}
        >
          {seg.state}
        </span>
        {seg.note === "ctx" && (
          <span className="rounded-sm border border-ctx/60 px-1.5 py-0.5 font-mono text-[10px] text-ink">
            ctx
          </span>
        )}
      </div>
      <dl className="mt-2 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 font-mono text-[11px]">
        <dt className="text-ink-dim">window</dt>
        <dd className="text-ink">
          {seg.t0} <span className="text-ink-dim">→</span> {seg.t1}
        </dd>
        <dt className="text-ink-dim">duration</dt>
        <dd className="text-ink">{seg.t1 - seg.t0}t</dd>
        {task && (
          <>
            <dt className="text-ink-dim">kind</dt>
            <dd className="text-ink">{task.kind}</dd>
            <dt className="text-ink-dim">priority</dt>
            <dd className="text-ink">{task.priority}</dd>
          </>
        )}
      </dl>
    </div>
  );
}

/* inspector ------------------------------------------------------------- */

export function EventInspector({
  selection,
  result,
  onClose,
  onSelectEvents,
  className,
}: EventInspectorProps) {
  const taskById = React.useMemo(() => {
    const map = new Map<string, TaskResult>();
    for (const t of result.tasks) map.set(t.id, t);
    return map;
  }, [result.tasks]);

  const segmentEvents = React.useMemo(() => {
    if (selection.kind !== "segment") return [];
    const { seg } = selection;
    return result.trace.filter(
      (e) => e.task === seg.task && e.t >= seg.t0 && e.t <= seg.t1,
    );
  }, [selection, result.trace]);

  return (
    <aside
      className={cn(
        "flex h-full w-full flex-col bg-panel lg:w-80 lg:shrink-0 lg:border-l",
        className,
      )}
      aria-label="Event inspector"
    >
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-line px-3">
        <span className="font-mono text-[10px] tracking-widest text-ink-dim">
          INSPECTOR
        </span>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="ml-auto h-7 w-7 cursor-pointer rounded-sm text-ink-dim hover:bg-accent/10 hover:text-ink"
          onClick={onClose}
          aria-label="Close inspector"
        >
          <X className="h-4 w-4" />
        </Button>
      </div>

      <div className="min-h-0 flex-1 space-y-3 overflow-y-auto p-3">
        {selection.kind === "marker" ? (
          selection.events.length === 0 ? (
            <p className="font-mono text-xs text-ink-dim">no events</p>
          ) : (
            selection.events.map((event) => (
              <EventCard key={event.seq} event={event} />
            ))
          )
        ) : (
          <>
            <SegmentSummary
              seg={selection.seg}
              task={taskById.get(selection.seg.task)}
            />
            <div>
              <p className="mb-1.5 font-mono text-[10px] tracking-widest text-ink-dim">
                EVENTS IN WINDOW · {segmentEvents.length}
              </p>
              <div className="space-y-1">
                {segmentEvents.length === 0 && (
                  <p className="font-mono text-[11px] text-ink-dim">—</p>
                )}
                {segmentEvents.map((event) => (
                  <button
                    key={event.seq}
                    type="button"
                    onClick={() => onSelectEvents([event])}
                    className="flex w-full cursor-pointer items-center gap-2 rounded-sm border border-line bg-bg px-2 py-1 text-left font-mono text-[10px] text-ink-dim transition-colors hover:border-accent/40 hover:text-ink focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
                  >
                    <span className="text-ink">t={event.t}</span>
                    <span className={CATEGORY_TEXT[eventCategory(event.type)]}>
                      {event.type}
                    </span>
                    <span className="ml-auto truncate">{event.res ?? ""}</span>
                  </button>
                ))}
              </div>
            </div>
          </>
        )}
      </div>
    </aside>
  );
}
