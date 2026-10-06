/**
 * MicroRT-Lab — gantt/trace lookup helpers shared by the timeline chips,
 * live LED board and CPU indicator.
 */

import type { GanttSeg, TaskState } from "@/lib/types";

/** per-task gantt segments (engine emits a t-sorted gantt) */
export function buildLanes(gantt: GanttSeg[]): Map<string, GanttSeg[]> {
  const lanes = new Map<string, GanttSeg[]>();
  for (const seg of gantt) {
    const list = lanes.get(seg.task);
    if (list) list.push(seg);
    else lanes.set(seg.task, [seg]);
  }
  return lanes;
}

/** state of one task at tick t (null = no segment, e.g. pre-arrival) */
export function stateAtTick(segs: GanttSeg[] | undefined, t: number): TaskState | null {
  if (!segs) return null;
  for (const seg of segs) {
    if (t >= seg.t0 && t < seg.t1) return seg.state;
  }
  return null;
}

/** the single RUNNING segment at tick t (ctx segments = CPU handover) */
export function runningAtTick(
  gantt: GanttSeg[],
  t: number,
): { seg: GanttSeg; ctx: boolean } | null {
  for (const seg of gantt) {
    if (seg.state === "RUNNING" && t >= seg.t0 && t < seg.t1) {
      return { seg, ctx: seg.note === "ctx" };
    }
  }
  return null;
}

/** index of the last trace event with t <= cursor (−1 when none) */
export function lastEventIndexAt(trace: readonly { t: number }[], t: number): number {
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

/** last event of one task at or before tick t (−1 when none) */
export function lastEventForTask(
  trace: readonly { t: number; task: string | null }[],
  task: string,
  t: number,
): number {
  let idx = lastEventIndexAt(trace, t);
  while (idx >= 0) {
    if (trace[idx].task === task) return idx;
    idx -= 1;
  }
  return -1;
}
