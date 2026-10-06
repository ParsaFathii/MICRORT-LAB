"use client";

/**
 * MicroRT-Lab — Resources view (05): live state of every mutex/sem/msgq/evflag
 * at the playback cursor, derived by replaying that resource's trace events.
 * Each panel also shows final engine statistics and a contention mini-strip
 * (click to seek).
 */

import * as React from "react";

import { cn } from "@/lib/utils";
import type { ResourceCfg, ResourceResult, SimulationResult, TraceEvent } from "@/lib/types";
import { useWorkstation } from "@/store/workstation";
import {
  contentionMarkers,
  deriveResourceLive,
  eventsByResource,
  type ResourceLive,
} from "@/components/views/resources/resourceState";
import { EmptyPanel, type SimDoc, SimGate, useTick, ViewHeader } from "@/components/views/shared";

const TYPE_CHIP: Record<string, string> = {
  mutex: "border-state-blocked/40 bg-state-blocked/10 text-state-blocked",
  sem: "border-state-waiting/40 bg-state-waiting/10 text-state-waiting",
  msgq: "border-accent/40 bg-accent/10 text-accent",
  evflags: "border-state-sleeping/40 bg-state-sleeping/10 text-state-sleeping",
};

function WaiterChips({ ids, empty = "—" }: { ids: string[]; empty?: string }) {
  if (ids.length === 0) {
    return <span className="font-mono text-[10px] text-ink-dim">{empty}</span>;
  }
  return (
    <span className="flex flex-wrap gap-1">
      {ids.map((id) => (
        <span
          key={id}
          className="rounded-sm border border-state-blocked/40 bg-state-blocked/10 px-1.5 py-0.5 font-mono text-[10px] text-state-blocked"
        >
          {id}
        </span>
      ))}
    </span>
  );
}

/* live-state block per resource kind ----------------------------------- */

function LiveBlock({ live, cfg }: { live: ResourceLive; cfg: ResourceCfg | undefined }) {
  if (live.kind === "mutex") {
    return (
      <div className="space-y-1.5">
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">OWNER</span>
          {live.owner ? (
            <span className="rounded-sm border border-accent/40 bg-accent/10 px-2 py-0.5 font-mono text-[11px] text-accent">
              {live.owner}
            </span>
          ) : (
            <span className="rounded-sm border border-line bg-bg px-2 py-0.5 font-mono text-[10px] text-ink-dim">
              FREE
            </span>
          )}
          {cfg?.protocol && (
            <span className="ml-auto font-mono text-[10px] text-ink-dim">protocol {cfg.protocol}</span>
          )}
        </div>
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">WAITERS</span>
          <WaiterChips ids={live.waiters} />
        </div>
      </div>
    );
  }
  if (live.kind === "sem") {
    return (
      <div className="space-y-1.5">
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">COUNT</span>
          <span className="font-mono text-sm text-accent tabular-nums">{live.count}</span>
          {typeof cfg?.max === "number" && (
            <span className="font-mono text-[10px] text-ink-dim">/ max {cfg.max}</span>
          )}
          {typeof cfg?.initial === "number" && (
            <span className="ml-auto font-mono text-[10px] text-ink-dim">
              initial {cfg.initial}
            </span>
          )}
        </div>
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">WAITERS</span>
          <WaiterChips ids={live.waiters} />
        </div>
      </div>
    );
  }
  if (live.kind === "msgq") {
    return (
      <div className="space-y-1.5">
        <div className="flex flex-wrap items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">BUFFERED</span>
          <span className="font-mono text-sm text-accent tabular-nums">{live.queue.length}</span>
          {typeof cfg?.capacity === "number" && (
            <span className="font-mono text-[10px] text-ink-dim">/ cap {cfg.capacity}</span>
          )}
          <span className="flex flex-wrap items-center gap-1">
            {live.queue.length > 0 ? (
              live.queue.map((msg, i) => (
                <span
                  key={`${i}-${msg}`}
                  className="rounded-sm border border-ok/40 bg-ok/10 px-1.5 py-0.5 font-mono text-[10px] text-ok tabular-nums"
                >
                  msg {msg}
                </span>
              ))
            ) : (
              <span className="font-mono text-[10px] text-ink-dim">empty</span>
            )}
          </span>
        </div>
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">SENDERS</span>
          <WaiterChips ids={live.senders} />
        </div>
        <div className="flex items-center gap-2">
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">RECEIVERS</span>
          <WaiterChips ids={live.receivers} />
        </div>
      </div>
    );
  }
  // evflags — 8 LED cells for bits 0..7
  const bits = Array.from({ length: 8 }, (_, i) => (live.flags >> i) & 1);
  return (
    <div className="space-y-1.5">
      <div className="flex items-center gap-2">
        <span className="font-mono text-[10px] tracking-widest text-ink-dim">FLAGS</span>
        <span className="font-mono text-sm text-state-sleeping tabular-nums">
          0x{live.flags.toString(16).toUpperCase()}
        </span>
        <span className="flex gap-1">
          {bits.map((bit, i) => (
            <span
              key={i}
              title={`bit ${i} = ${bit}`}
              className={cn(
                "flex h-4 w-4 items-center justify-center rounded-sm border font-mono text-[9px]",
                bit
                  ? "border-state-sleeping bg-state-sleeping/25 text-state-sleeping"
                  : "border-line bg-bg text-ink-dim/60",
              )}
            >
              {i}
            </span>
          ))}
        </span>
      </div>
      <div className="flex items-center gap-2">
        <span className="font-mono text-[10px] tracking-widest text-ink-dim">WAITERS</span>
        <WaiterChips ids={live.waiters} />
      </div>
    </div>
  );
}

/* contention mini timeline strip ---------------------------------------- */

function TimelineStrip({
  markers,
  horizon,
  tick,
  onSeek,
  label,
}: {
  markers: { t: number; seq: number; label: string }[];
  horizon: number;
  tick: number;
  onSeek: (t: number) => void;
  label: string;
}) {
  const span = Math.max(1, horizon);
  return (
    <button
      type="button"
      aria-label={`${label} — contention timeline, click to seek`}
      className="group relative block h-6 w-full cursor-pointer overflow-hidden rounded-sm border border-line bg-bg focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
      onClick={(e) => {
        const rect = e.currentTarget.getBoundingClientRect();
        const frac = (e.clientX - rect.left) / Math.max(1, rect.width);
        onSeek(Math.round(frac * span));
      }}
    >
      <span className="absolute inset-x-1 top-1/2 h-px bg-line" aria-hidden />
      {markers.map((m) => (
        <span
          key={m.seq}
          title={m.label}
          aria-hidden
          className="absolute top-1 h-4 w-0.5 -translate-y-0.5 bg-bad/80 group-hover:bg-bad"
          style={{ left: `${Math.min(100, (m.t / span) * 100)}%` }}
        />
      ))}
      <span
        aria-hidden
        className="absolute top-0 h-full w-0.5 bg-accent"
        style={{ left: `${Math.min(100, (tick / span) * 100)}%` }}
      />
      <span className="absolute right-1 bottom-0 font-mono text-[8px] text-ink-dim/70">
        t=0…{horizon}
      </span>
    </button>
  );
}

/* one resource panel ----------------------------------------------------- */

function ResourcePanel({
  rec,
  cfg,
  events,
  tick,
  horizon,
  onSeek,
}: {
  rec: ResourceResult;
  cfg: ResourceCfg | undefined;
  events: TraceEvent[];
  tick: number;
  horizon: number;
  onSeek: (t: number) => void;
}) {
  const live = React.useMemo(
    () =>
      deriveResourceLive(events, rec.type, tick, {
        semInitial: cfg?.initial,
        capacity: cfg?.capacity,
      }),
    [events, rec.type, tick, cfg?.initial, cfg?.capacity],
  );
  const markers = React.useMemo(
    () => contentionMarkers(events, rec.type),
    [events, rec.type],
  );

  return (
    <section className="rounded-lg border border-line bg-panel">
      <header className="flex flex-wrap items-center gap-2 border-b border-line px-3 py-2">
        <span
          className={cn(
            "rounded-sm border px-1.5 py-0.5 font-mono text-[10px] tracking-wide",
            TYPE_CHIP[rec.type] ?? "border-line bg-muted text-ink-dim",
          )}
        >
          {rec.type}
        </span>
        <span className="font-mono text-xs text-ink">{rec.id}</span>
        <span className="ml-auto font-mono text-[10px] text-ink-dim">at t={tick}</span>
      </header>
      <div className="space-y-2.5 p-3">
        <LiveBlock live={live} cfg={cfg} />
        <div className="flex flex-wrap gap-x-4 gap-y-1 font-mono text-[10px] text-ink-dim">
          <span>
            ACQ <span className="text-ink tabular-nums">{rec.acquisitions}</span>
          </span>
          <span>
            CONT <span className="text-ink tabular-nums">{rec.contentions}</span>
          </span>
          <span>
            Q <span className="text-ink tabular-nums">{rec.queueDepth}</span>
          </span>
          <span>
            FINAL{" "}
            <span className="text-ink">{rec.finalOwner ?? rec.holderAtEnd ?? "free"}</span>
          </span>
        </div>
        <TimelineStrip
          markers={markers}
          horizon={horizon}
          tick={tick}
          onSeek={onSeek}
          label={rec.id}
        />
      </div>
    </section>
  );
}

/* view ------------------------------------------------------------------- */

export function ResourcesView() {
  return <SimGate render={(doc: SimDoc) => <ResourcesBody key={doc.simId} result={doc.result} />} />;
}

function ResourcesBody({ result }: { result: SimulationResult }) {
  const horizon = result.simulatedUntil;
  const tick = useTick(horizon);
  const setCursor = useWorkstation((s) => s.setCursor);
  const pause = useWorkstation((s) => s.pause);

  const cfgMap = React.useMemo(() => {
    const m = new Map<string, ResourceCfg>();
    for (const r of result.config.resources ?? []) m.set(r.id, r);
    return m;
  }, [result.config.resources]);

  const eventsMap = React.useMemo(
    () => eventsByResource(result.trace, result.resources.map((r) => r.id)),
    [result.trace, result.resources],
  );

  const onSeek = (t: number) => {
    pause();
    setCursor(t);
  };

  if (result.resources.length === 0) {
    return (
      <div className="flex h-full min-h-0 flex-col">
        <ViewHeader num="05" label="Resources" hint="state at the playback cursor" />
        <div className="p-4">
          <EmptyPanel
            label="NO RESOURCES CONFIGURED"
            hint="This simulation declares no mutexes, semaphores, message queues or event flags."
          />
        </div>
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader
        num="05"
        label="Resources"
        hint="live state at the playback cursor — replayed from the trace"
      />
      <div className="min-h-0 flex-1 overflow-y-auto p-4">
        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          {result.resources.map((rec) => (
            <ResourcePanel
              key={rec.id}
              rec={rec}
              cfg={cfgMap.get(rec.id)}
              events={eventsMap.get(rec.id) ?? []}
              tick={tick}
              horizon={horizon}
              onSeek={onSeek}
            />
          ))}
        </div>
      </div>
    </div>
  );
}
