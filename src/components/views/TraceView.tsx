"use client";

/**
 * MicroRT-Lab — Trace view (04): dense event trace browser.
 *
 * Toolbar: type filter (multi-select popover with counts), task filter chips
 * (shared store exclusion list), free-text search, "cursor ≥" toggle, CSV
 * export. Rows render in a windowed slice (1000 + load more); row click opens
 * the full event in a Sheet drawer; the row at the playback cursor is
 * highlighted and auto-followed while playing.
 */

import * as React from "react";
import { ChevronDown, Download, Filter, Search, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Input } from "@/components/ui/input";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { Sheet, SheetContent, SheetHeader, SheetTitle } from "@/components/ui/sheet";
import { cn } from "@/lib/utils";
import type { TraceEvent } from "@/lib/types";
import { useWorkstation } from "@/store/workstation";
import { CATEGORY_CHIP, eventCategory } from "@/components/timeline/eventColors";
import { downloadBlob, type SimDoc, SimGate, useTick, ViewHeader } from "@/components/views/shared";
import { toast } from "sonner";

const PAGE = 1000;

/* ---------------------------------------------------------------------- */
/* rows                                                                    */
/* ---------------------------------------------------------------------- */

function compactDetail(detail: Record<string, unknown>): string {
  const s = JSON.stringify(detail);
  return s === "{}" ? "" : s;
}

interface RowProps {
  event: TraceEvent;
  atCursor: boolean;
  selected: boolean;
  onOpen: (event: TraceEvent) => void;
}

const TraceRow = React.memo(function TraceRow({ event, atCursor, selected, onOpen }: RowProps) {
  const detail = compactDetail(event.detail);
  return (
    <tr
      data-seq={event.seq}
      tabIndex={0}
      aria-label={`Event ${event.seq} at t=${event.t}: ${event.type}${event.task ? ` task ${event.task}` : ""}`}
      onClick={() => onOpen(event)}
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          onOpen(event);
        }
      }}
      className={cn(
        "cursor-pointer border-b border-line/50 transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-accent",
        atCursor && "bg-accent/10",
        selected && "bg-accent/5 ring-1 ring-inset ring-accent/50",
        "hover:bg-accent/5",
      )}
    >
      <td className="whitespace-nowrap px-2 py-1 text-ink-dim tabular-nums">{event.seq}</td>
      <td className="whitespace-nowrap px-2 py-1 text-accent tabular-nums">{event.t}</td>
      <td className="px-2 py-1">
        <span
          className={cn(
            "inline-block rounded-sm border px-1.5 py-0.5 font-mono text-[10px] leading-3 tracking-wide whitespace-nowrap",
            CATEGORY_CHIP[eventCategory(event.type)],
          )}
        >
          {event.type}
        </span>
      </td>
      <td className="max-w-28 truncate px-2 py-1 text-ink">{event.task ?? "—"}</td>
      <td className="max-w-24 truncate px-2 py-1 text-ink">{event.res ?? "—"}</td>
      <td className="whitespace-nowrap px-2 py-1 text-[10px] text-ink-dim">
        {event.from || event.to ? (
          <>
            {event.from ?? "—"} <span className="text-ink-dim/60">→</span> {event.to ?? "—"}
          </>
        ) : (
          "—"
        )}
      </td>
      <td className="whitespace-nowrap px-2 py-1 text-right text-ink-dim tabular-nums">
        {event.dur ?? "—"}
      </td>
      <td
        className="max-w-64 truncate px-2 py-1 text-[10px] text-ink-dim"
        title={detail || undefined}
      >
        {detail || "—"}
      </td>
    </tr>
  );
});

/* ---------------------------------------------------------------------- */
/* toolbar bits                                                            */
/* ---------------------------------------------------------------------- */

function TypeFilterPopover({
  counts,
  enabled,
  onToggle,
  onAll,
}: {
  counts: { type: string; count: number }[];
  enabled: Set<string>;
  onToggle: (type: string) => void;
  onAll: () => void;
}) {
  const [open, setOpen] = React.useState(false);
  const active = counts.length - enabled.size;
  return (
    <Popover open={open} onOpenChange={setOpen}>
      <PopoverTrigger asChild>
        <Button
          type="button"
          variant="outline"
          size="sm"
          className="h-8 cursor-pointer gap-1.5 rounded-md px-2 font-mono text-[11px]"
          aria-label="Filter event types"
        >
          <Filter className="h-3.5 w-3.5" aria-hidden />
          TYPE{active > 0 ? ` ·${active} off` : ""}
          <ChevronDown className="h-3 w-3" aria-hidden />
        </Button>
      </PopoverTrigger>
      <PopoverContent align="start" className="w-56 p-2">
        <div className="flex items-center justify-between px-1 pb-1">
          <p className="font-mono text-[10px] tracking-widest text-ink-dim">EVENT TYPES</p>
          <button
            type="button"
            onClick={onAll}
            className="cursor-pointer rounded-sm px-1 font-mono text-[10px] text-accent hover:bg-accent/10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
          >
            ALL
          </button>
        </div>
        <div className="max-h-72 overflow-y-auto">
          {counts.map(({ type, count }) => {
            const checked = enabled.has(type);
            return (
              <label
                key={type}
                className="flex cursor-pointer items-center gap-2 rounded-sm px-1 py-1 hover:bg-accent/5"
              >
                <Checkbox
                  checked={checked}
                  onCheckedChange={() => onToggle(type)}
                  aria-label={`Toggle ${type}`}
                />
                <span
                  className={cn(
                    "font-mono text-[10px] leading-4",
                    CATEGORY_CHIP[eventCategory(type)],
                    "rounded-sm border px-1",
                  )}
                >
                  {type}
                </span>
                <span className="ml-auto font-mono text-[10px] text-ink-dim tabular-nums">
                  {count}
                </span>
              </label>
            );
          })}
        </div>
      </PopoverContent>
    </Popover>
  );
}

function EventDrawer({
  event,
  onClose,
  onSeek,
}: {
  event: TraceEvent;
  onClose: () => void;
  onSeek: (t: number) => void;
}) {
  return (
    <Sheet open onOpenChange={(open) => !open && onClose()}>
      <SheetContent side="right" className="w-full overflow-y-auto sm:max-w-md">
        <SheetHeader className="border-b border-line">
          <SheetTitle className="flex flex-wrap items-center gap-2 font-mono text-xs">
            <span
              className={cn(
                "rounded-sm border px-1.5 py-0.5 text-[10px]",
                CATEGORY_CHIP[eventCategory(event.type)],
              )}
            >
              {event.type}
            </span>
            <span className="text-ink-dim">seq {event.seq}</span>
            <span className="text-accent">t={event.t}</span>
          </SheetTitle>
        </SheetHeader>
        <div className="space-y-3 p-4">
          <dl className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 font-mono text-[11px]">
            {[
              ["task", event.task],
              ["res", event.res],
              ["from", event.from],
              ["to", event.to],
              ["dur", event.dur],
            ].map(([label, value]) => (
              <React.Fragment key={String(label)}>
                <dt className="text-ink-dim">{label}</dt>
                <dd className="text-ink">{value === null || value === undefined ? "—" : String(value)}</dd>
              </React.Fragment>
            ))}
          </dl>
          <div>
            <p className="mb-1 font-mono text-[9px] tracking-widest text-ink-dim">DETAIL (FULL)</p>
            <pre className="max-h-72 overflow-auto rounded-md border border-line bg-bg p-2 font-mono text-[10px] leading-4 text-ink-dim">
              {JSON.stringify(event.detail, null, 2)}
            </pre>
          </div>
          <div>
            <p className="mb-1 font-mono text-[9px] tracking-widest text-ink-dim">RAW EVENT</p>
            <pre className="max-h-96 overflow-auto rounded-md border border-line bg-bg p-2 font-mono text-[10px] leading-4 text-ink-dim">
              {JSON.stringify(event, null, 2)}
            </pre>
          </div>
          <Button
            type="button"
            variant="outline"
            className="w-full cursor-pointer rounded-md font-mono text-[11px] hover:bg-accent/10"
            onClick={() => {
              onSeek(event.t);
              onClose();
            }}
          >
            SEEK TO t={event.t}
          </Button>
        </div>
      </SheetContent>
    </Sheet>
  );
}

/* ---------------------------------------------------------------------- */
/* CSV                                                                     */
/* ---------------------------------------------------------------------- */

function csvEscape(value: string): string {
  return /[",\n]/.test(value) ? `"${value.replace(/"/g, '""')}"` : value;
}

function exportCsv(rows: TraceEvent[], simName: string): void {
  const header = ["seq", "t", "type", "task", "res", "from", "to", "dur", "detail"];
  const lines = rows.map((e) =>
    [
      String(e.seq),
      String(e.t),
      e.type,
      e.task ?? "",
      e.res ?? "",
      e.from ?? "",
      e.to ?? "",
      e.dur === null ? "" : String(e.dur),
      JSON.stringify(e.detail),
    ]
      .map(csvEscape)
      .join(","),
  );
  const filename = `micrort-trace-${simName.replace(/[^a-zA-Z0-9_-]+/g, "_") || "sim"}.csv`;
  downloadBlob(filename, [header.join(","), ...lines].join("\n"), "text/csv");
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function TraceView() {
  return (
    <SimGate
      render={(doc: SimDoc) => (
        <TraceBody key={doc.simId} doc={doc} horizon={doc.result.simulatedUntil} />
      )}
    />
  );
}

function TraceBody({ doc, horizon }: { doc: SimDoc; horizon: number }) {
  const result = doc.result;
  const tick = useTick(horizon);
  const playing = useWorkstation((s) => s.playback.playing);
  const setCursor = useWorkstation((s) => s.setCursor);
  const pause = useWorkstation((s) => s.pause);
  const hiddenTasks = useWorkstation((s) => s.filters.tasks);
  const toggleTaskFilter = useWorkstation((s) => s.toggleTaskFilter);
  const eventTypes = useWorkstation((s) => s.filters.eventTypes);
  const setEventTypeFilters = useWorkstation((s) => s.setEventTypeFilters);

  const [search, setSearch] = React.useState("");
  const [cursorFilter, setCursorFilter] = React.useState(false);
  const [follow, setFollow] = React.useState(true);
  const [limit, setLimit] = React.useState(PAGE);
  const [selected, setSelected] = React.useState<TraceEvent | null>(null);
  const scrollRef = React.useRef<HTMLDivElement | null>(null);

  /* per-type counts from the loaded result (client-side) */
  const typeCounts = React.useMemo(() => {
    const counts = new Map<string, number>();
    for (const e of result.trace) counts.set(e.type, (counts.get(e.type) ?? 0) + 1);
    return [...counts.entries()]
      .map(([type, count]) => ({ type, count }))
      .sort((a, b) => b.count - a.count || a.type.localeCompare(b.type));
  }, [result.trace]);

  const allTypes = React.useMemo(() => typeCounts.map((c) => c.type), [typeCounts]);
  const enabledTypes = React.useMemo(
    () => (eventTypes.length === 0 ? new Set(allTypes) : new Set(eventTypes)),
    [eventTypes, allTypes],
  );

  const toggleType = (type: string) => {
    const next = new Set(enabledTypes);
    if (next.has(type)) next.delete(type);
    else next.add(type);
    if (next.size === 0) return; // keep at least one type enabled
    // all selected → store empty list (= all visible)
    setEventTypeFilters(next.size === allTypes.length ? [] : [...next]);
  };

  /* filtered rows */
  const filtered = React.useMemo(() => {
    const q = search.trim().toLowerCase();
    return result.trace.filter((e) => {
      if (!enabledTypes.has(e.type)) return false;
      if (hiddenTasks.includes(e.task ?? "")) return false;
      if (cursorFilter && e.t > tick) return false;
      if (q) {
        const hay = `${e.type} ${e.task ?? ""} ${e.res ?? ""} ${compactDetail(e.detail)}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });
  }, [result.trace, enabledTypes, hiddenTasks, cursorFilter, tick, search]);

  const visible = filtered.slice(0, limit);
  const cursorSeq = React.useMemo(() => {
    const first = filtered.find((e) => e.t >= tick);
    return (first ?? filtered[filtered.length - 1])?.seq ?? null;
  }, [filtered, tick]);

  /* auto-scroll to the cursor row while playing */
  React.useEffect(() => {
    if (!playing || !follow || cursorSeq === null) return;
    const container = scrollRef.current;
    if (!container) return;
    const el = container.querySelector(`tr[data-seq="${cursorSeq}"]`);
    el?.scrollIntoView({ block: "nearest" });
  }, [playing, follow, cursorSeq]);

  const seek = (t: number) => {
    pause();
    setCursor(t);
  };

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="04" label="Trace" hint="event trace browser — click a row for the full record">
        <TypeFilterPopover
          counts={typeCounts}
          enabled={enabledTypes}
          onToggle={toggleType}
          onAll={() => setEventTypeFilters([])}
        />
        <div className="relative">
          <Search
            className="pointer-events-none absolute top-1/2 left-2 h-3.5 w-3.5 -translate-y-1/2 text-ink-dim"
            aria-hidden
          />
          <Input
            value={search}
            onChange={(e) => {
              setSearch(e.target.value);
              setLimit(PAGE);
            }}
            placeholder="search type / task / res / detail"
            aria-label="Search trace events"
            className="h-8 w-52 rounded-md pl-7 font-mono text-[11px]"
          />
          {search && (
            <button
              type="button"
              aria-label="Clear search"
              onClick={() => setSearch("")}
              className="absolute top-1/2 right-1.5 -translate-y-1/2 cursor-pointer rounded-sm p-0.5 text-ink-dim hover:text-ink focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
            >
              <X className="h-3 w-3" aria-hidden />
            </button>
          )}
        </div>
        <Button
          type="button"
          size="sm"
          variant={cursorFilter ? "default" : "outline"}
          aria-pressed={cursorFilter}
          className="h-8 cursor-pointer rounded-md font-mono text-[11px]"
          onClick={() => setCursorFilter((v) => !v)}
        >
          CURSOR ≥ t={tick}
        </Button>
        <Button
          type="button"
          size="sm"
          variant={follow ? "default" : "outline"}
          aria-pressed={follow}
          className="h-8 cursor-pointer rounded-md font-mono text-[11px]"
          onClick={() => setFollow((v) => !v)}
        >
          FOLLOW
        </Button>
        <Button
          type="button"
          size="sm"
          variant="outline"
          className="h-8 cursor-pointer gap-1.5 rounded-md font-mono text-[11px] hover:bg-accent/10"
          onClick={() => {
            exportCsv(filtered, doc.result.config.name);
            toast.success(`Exported ${filtered.length} events to CSV`);
          }}
          aria-label="Export filtered events as CSV"
        >
          <Download className="h-3.5 w-3.5" aria-hidden />
          CSV
        </Button>
      </ViewHeader>

      {/* task chips (shared hidden-ids exclusion filter) */}
      <div className="flex shrink-0 flex-wrap items-center gap-1.5 border-b border-line bg-panel px-3 py-1.5">
        <span className="mr-1 font-mono text-[10px] tracking-widest text-ink-dim">TASKS</span>
        {result.tasks.map((task) => {
          const hidden = hiddenTasks.includes(task.id);
          return (
            <button
              key={task.id}
              type="button"
              onClick={() => toggleTaskFilter(task.id)}
              aria-pressed={!hidden}
              aria-label={`Toggle task ${task.id} visibility`}
              className={cn(
                "flex cursor-pointer items-center rounded-sm border px-2 py-0.5 font-mono text-[11px] transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
                hidden
                  ? "border-line bg-transparent text-ink-dim/50 line-through"
                  : "border-line bg-bg text-ink hover:border-accent/40",
              )}
            >
              {task.id}
            </button>
          );
        })}
      </div>

      {/* table */}
      <div className="flex min-h-0 flex-1 flex-col">
        <div ref={scrollRef} className="min-h-0 flex-1 overflow-auto">
          <table className="w-full border-collapse font-mono text-[11px]">
            <thead className="sticky top-0 z-10">
              <tr className="bg-panel text-left">
                {["SEQ", "T", "TYPE", "TASK", "RES", "FROM→TO", "DUR", "DETAIL"].map((h, i) => (
                  <th
                    key={h}
                    className={cn(
                      "border-b border-line px-2 py-1.5 font-mono text-[9px] font-normal tracking-widest text-ink-dim",
                      i === 6 && "text-right",
                    )}
                  >
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {visible.map((event) => (
                <TraceRow
                  key={event.seq}
                  event={event}
                  atCursor={event.t === tick}
                  selected={selected?.seq === event.seq}
                  onOpen={setSelected}
                />
              ))}
            </tbody>
          </table>
          {visible.length === 0 && (
            <div className="flex min-h-32 items-center justify-center">
              <p className="font-mono text-[10px] tracking-widest text-ink-dim">
                {result.trace.length === 0
                  ? "NO EVENTS"
                  : "NO EVENTS MATCH THE CURRENT FILTERS"}
              </p>
            </div>
          )}
        </div>
        {filtered.length > visible.length && (
          <div className="flex shrink-0 items-center justify-center gap-3 border-t border-line bg-panel px-3 py-2">
            <p className="font-mono text-[10px] text-ink-dim">
              SHOWING {visible.length}/{filtered.length} EVENTS
            </p>
            <Button
              type="button"
              size="sm"
              variant="outline"
              className="h-7 cursor-pointer rounded-md font-mono text-[10px] hover:bg-accent/10"
              onClick={() => setLimit((l) => l + PAGE)}
            >
              LOAD {Math.min(PAGE, filtered.length - visible.length)} MORE
            </Button>
          </div>
        )}
        {filtered.length <= visible.length && filtered.length > 0 && (
          <div className="shrink-0 border-t border-line bg-panel px-3 py-1.5 text-center">
            <p className="font-mono text-[10px] text-ink-dim">
              {filtered.length} EVENT{filtered.length === 1 ? "" : "S"}
              {cursorFilter ? " · CURSOR-FILTERED" : ""}
              {search ? " · SEARCH-FILTERED" : ""}
            </p>
          </div>
        )}
      </div>

      {selected && (
        <EventDrawer event={selected} onClose={() => setSelected(null)} onSeek={seek} />
      )}
    </div>
  );
}
