"use client";

/**
 * MicroRT-Lab — Gantt timeline legend: state swatches + event markers.
 */

import { cn } from "@/lib/utils";

const STATE_SWATCHES: { label: string; swatch: string }[] = [
  { label: "running", swatch: "bg-state-running" },
  { label: "ready", swatch: "bg-state-ready/60" },
  { label: "blocked", swatch: "bg-state-blocked/85" },
  { label: "waiting", swatch: "bg-state-waiting/85" },
  { label: "sleeping", swatch: "bg-state-sleeping/85" },
  {
    label: "terminated",
    swatch: "bg-[repeating-linear-gradient(45deg,var(--state-terminated)_0_3px,transparent_3px_6px)]",
  },
  { label: "ctx (dashed)", swatch: "bg-ctx/40 border border-dashed border-ctx" },
];

const MARKERS: { glyph: string; className: string; label: string }[] = [
  { glyph: "◆", className: "text-bad", label: "preempt" },
  { glyph: "▮", className: "text-bad", label: "deadline miss / deadlock" },
  { glyph: "■", className: "text-ctx", label: "ctx switch" },
  { glyph: "●", className: "text-ok", label: "complete" },
  { glyph: "│", className: "text-accent", label: "time cursor" },
];

export function TimelineLegend({ className }: { className?: string }) {
  return (
    <div
      className={cn(
        "flex flex-wrap items-center gap-x-3 gap-y-1",
        className,
      )}
      aria-label="Timeline legend"
    >
      {STATE_SWATCHES.map((s) => (
        <span key={s.label} className="flex items-center gap-1.5">
          <span
            aria-hidden
            className={cn("h-2.5 w-4 rounded-[2px]", s.swatch)}
          />
          <span className="font-mono text-[9px] uppercase tracking-wider text-ink-dim">
            {s.label}
          </span>
        </span>
      ))}
      <span aria-hidden className="mx-1 h-3 w-px bg-line" />
      {MARKERS.map((m) => (
        <span key={m.label} className="flex items-center gap-1">
          <span aria-hidden className={cn("font-mono text-[10px] leading-none", m.className)}>
            {m.glyph}
          </span>
          <span className="font-mono text-[9px] uppercase tracking-wider text-ink-dim">
            {m.label}
          </span>
        </span>
      ))}
    </div>
  );
}
