"use client";

/**
 * MicroRT-Lab — shared building blocks for stage-2 views.
 *
 * Every data view follows the same skeleton via <SimGate/>:
 *   no sim → NoSimPlaceholder · loading → skeleton · error → retry panel ·
 *   no result document → empty panel · else render(result).
 */

import * as React from "react";
import type { UseQueryResult } from "@tanstack/react-query";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";
import type { SimulationRecord, SimulationResult } from "@/lib/types";
import { useSimulation } from "@/hooks/useSimulation";
import { useWorkstation } from "@/store/workstation";
import { NoSimPlaceholder } from "@/components/views/registry";

/* ---------------------------------------------------------------------------
 * Panel / tile primitives (instrument aesthetic)
 * ------------------------------------------------------------------------- */

export function ViewHeader({
  num,
  label,
  hint,
  children,
}: {
  num: string;
  label: string;
  hint?: string;
  children?: React.ReactNode;
}) {
  return (
    <div className="flex shrink-0 flex-wrap items-center gap-x-3 gap-y-2 border-b border-line bg-panel px-3 py-2">
      <p className="font-mono text-[10px] tracking-widest text-ink-dim">
        {num} · {label.toUpperCase()}
      </p>
      {hint && <p className="hidden font-mono text-[10px] text-ink-dim/70 md:block">{hint}</p>}
      <div className="ml-auto flex flex-wrap items-center gap-2">{children}</div>
    </div>
  );
}

export function StatTile({
  label,
  value,
  sub,
  tone = "ink",
  className,
}: {
  label: string;
  value: React.ReactNode;
  sub?: React.ReactNode;
  /** text color tone for the value */
  tone?: "ink" | "accent" | "ok" | "bad" | "dim";
  className?: string;
}) {
  const toneClass =
    tone === "accent"
      ? "text-accent"
      : tone === "ok"
        ? "text-ok"
        : tone === "bad"
          ? "text-bad"
          : tone === "dim"
            ? "text-ink-dim"
            : "text-ink";
  return (
    <div className={cn("rounded-md border border-line bg-panel px-3 py-2", className)}>
      <p className="font-mono text-[10px] uppercase tracking-widest text-ink-dim">{label}</p>
      <p className={cn("mt-1 font-mono text-lg leading-6 tabular-nums", toneClass)}>{value}</p>
      {sub !== undefined && (
        <p className="mt-0.5 font-mono text-[10px] leading-3 text-ink-dim">{sub}</p>
      )}
    </div>
  );
}

export function SectionPanel({
  title,
  children,
  className,
  right,
}: {
  title: string;
  children: React.ReactNode;
  className?: string;
  right?: React.ReactNode;
}) {
  return (
    <section className={cn("rounded-lg border border-line bg-panel", className)}>
      <header className="flex items-center gap-2 border-b border-line px-3 py-2">
        <h3 className="font-mono text-[10px] uppercase tracking-widest text-ink-dim">{title}</h3>
        {right && <div className="ml-auto flex items-center gap-2">{right}</div>}
      </header>
      <div className="p-3">{children}</div>
    </section>
  );
}

export function ErrorPanel({ message, onRetry }: { message: string; onRetry: () => void }) {
  return (
    <div className="flex h-full items-center justify-center p-6">
      <div className="w-full max-w-md rounded-lg border border-bad/40 bg-panel p-6">
        <p className="font-mono text-[10px] tracking-widest text-bad">DATA ERROR</p>
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

export function EmptyPanel({ label, hint }: { label: string; hint?: string }) {
  return (
    <div className="flex min-h-24 items-center justify-center rounded-lg border border-dashed border-line p-4">
      <div className="text-center">
        <p className="font-mono text-[10px] uppercase tracking-widest text-ink-dim/70">{label}</p>
        {hint && <p className="mt-1 text-xs text-ink-dim">{hint}</p>}
      </div>
    </div>
  );
}

/* ---------------------------------------------------------------------------
 * Playback helpers
 * ------------------------------------------------------------------------- */

/** Integer tick at the playback cursor, clamped to the horizon. */
export function useTick(horizon: number): number {
  const cursor = useWorkstation((s) => s.playback.cursorTick);
  return Math.max(0, Math.min(Math.floor(cursor), Math.max(0, horizon)));
}

/* ---------------------------------------------------------------------------
 * Task color palette (fixed hue assignment, amber/teal/rose/lime/stone)
 * ------------------------------------------------------------------------- */

const TASK_HUES = [
  "#f5b642", // amber
  "#45b3a4", // teal
  "#e4574d", // rose
  "#a3c949", // lime
  "#8a8271", // stone
  "#d97706", // amber deep
  "#0f766e", // teal deep
  "#be123c", // rose deep
  "#4d7c0f", // lime deep
  "#57503f", // stone deep
] as const;

export function taskHue(taskId: string, taskIds: readonly string[]): string {
  const idx = taskIds.indexOf(taskId);
  return TASK_HUES[(idx < 0 ? taskId.length : idx) % TASK_HUES.length];
}

/** map of task id → hue for one result (declaration order = hue order) */
export function taskHueMap(tasks: readonly { id: string }[]): Map<string, string> {
  return new Map(tasks.map((t, i) => [t.id, TASK_HUES[i % TASK_HUES.length]]));
}

/* ---------------------------------------------------------------------------
 * File export (client-side blob download)
 * ------------------------------------------------------------------------- */

export function downloadBlob(filename: string, content: string, mime: string): void {
  const blob = new Blob([content], { type: `${mime};charset=utf-8` });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.rel = "noopener";
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

export function formatBytes(n: number): string {
  return `${n} B`;
}

/* ---------------------------------------------------------------------------
 * SimGate — the standard data-view skeleton
 * ------------------------------------------------------------------------- */

export interface SimDoc {
  simId: string;
  record: SimulationRecord;
  result: SimulationResult;
}

export function SimGate({ render }: { render: (doc: SimDoc) => React.ReactNode }) {
  const simId = useWorkstation((s) => s.simId);
  const query: UseQueryResult<SimulationRecord> = useSimulation(simId);

  if (simId === null) return <NoSimPlaceholder />;
  if (query.isLoading) {
    return (
      <div className="space-y-3 p-4">
        <Skeleton className="h-8 w-2/3" />
        <Skeleton className="h-64 w-full" />
      </div>
    );
  }
  if (query.isError) {
    return (
      <ErrorPanel
        message={query.error instanceof Error ? query.error.message : "load failed"}
        onRetry={() => void query.refetch()}
      />
    );
  }
  const record = query.data;
  const result = record?.result ?? null;
  if (!record || !result) {
    return (
      <div className="flex h-full items-center justify-center p-6">
        <div className="text-center">
          <p className="font-mono text-[10px] tracking-widest text-ink-dim">NO RESULT DOCUMENT</p>
          <p className="mt-2 text-sm text-ink-dim">
            This simulation has not been run yet — run it from the Workbench.
          </p>
        </div>
      </div>
    );
  }
  return <>{render({ simId, record, result })}</>;
}
