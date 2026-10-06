/**
 * MicroRT-Lab — view registry (the ONLY place views are wired).
 *
 * ── STAGE-2 EXTENSION CONTRACT ────────────────────────────────────────────
 * To add a view: create `src/components/views/<Name>View.tsx` (client
 * component, default or named export), import it here, and replace the
 * `comingOnline(...)` entry for its ViewId in the VIEWS record below.
 * The ViewId union itself lives in src/store/workstation.ts (VIEW_IDS) —
 * add the id there first if it does not exist yet. The shell rail, keyboard
 * shortcuts and view labels read VIEW_META / VIEWS from this file, so no
 * shell changes are ever needed.
 *
 * Views that require a loaded simulation should render <NoSimPlaceholder />
 * when `simId` from the store is null; fetch data with the hooks in
 * src/hooks/useSimulation.ts and read playback/filters from
 * src/store/workstation.ts.
 * ──────────────────────────────────────────────────────────────────────────
 */

import type { ComponentType } from "react";

import type { ViewId } from "@/store/workstation";

export type { ViewId };

import { LiveView } from "@/components/views/LiveView";
import { TimelineView } from "@/components/views/TimelineView";
import { WorkbenchView } from "@/components/views/WorkbenchView";

export interface ViewMeta {
  /** rail number, e.g. "01" */
  num: string;
  /** human label */
  label: string;
}

export const VIEW_META: Record<ViewId, ViewMeta> = {
  workbench: { num: "01", label: "Workbench" },
  timeline: { num: "02", label: "Timeline" },
  live: { num: "03", label: "Live" },
  trace: { num: "04", label: "Trace" },
  resources: { num: "05", label: "Resources" },
  deadlock: { num: "06", label: "Deadlock" },
  memory: { num: "07", label: "Memory" },
  metrics: { num: "08", label: "Metrics" },
  compare: { num: "09", label: "Compare" },
  reports: { num: "10", label: "Reports" },
  docs: { num: "11", label: "Docs" },
};

/** Placeholder panel for views that stage 2 replaces. */
function comingOnline(view: ViewId): ComponentType {
  const meta = VIEW_META[view];
  function ComingOnline() {
    return (
      <div className="flex h-full items-center justify-center p-6">
        <div className="w-full max-w-md rounded-lg border border-line bg-panel p-6 text-center">
          <p className="font-mono text-[10px] tracking-widest text-ink-dim">
            VIEW {meta.num}
          </p>
          <h2 className="mt-2 font-mono text-sm text-ink">{meta.label}</h2>
          <p className="mt-3 text-xs text-ink-dim">
            Coming online in a later stage. The workstation shell, store and
            data layer are already wired for this view.
          </p>
        </div>
      </div>
    );
  }
  return ComingOnline;
}

export const VIEWS: Record<ViewId, ComponentType> = {
  workbench: WorkbenchView,
  timeline: TimelineView,
  live: LiveView,
  trace: comingOnline("trace"),
  resources: comingOnline("resources"),
  deadlock: comingOnline("deadlock"),
  memory: comingOnline("memory"),
  metrics: comingOnline("metrics"),
  compare: comingOnline("compare"),
  reports: comingOnline("reports"),
  docs: comingOnline("docs"),
};

/** Shared empty state for views that need a loaded simulation. */
export function NoSimPlaceholder() {
  return (
    <div className="flex h-full items-center justify-center p-6">
      <div className="text-center">
        <p className="font-mono text-[10px] tracking-widest text-ink-dim">
          NO SIMULATION LOADED
        </p>
        <p className="mt-2 text-sm text-ink-dim">
          Load or run an experiment in the Workbench.
        </p>
      </div>
    </div>
  );
}
