/**
 * MicroRT-Lab — live resource state derivation (Resources/Deadlock views).
 *
 * Walks the trace of one resource up to tick t and reconstructs the current
 * owner / semaphore count / message queue / event-flag bits. Assumptions for
 * hand-off cases (blocked senders, woken sem waiters) follow the engine
 * semantics documented in docs/spec/SIMULATION_SCHEMA.md; they are marked
 * inline where the trace alone cannot distinguish them.
 */

import type { TraceEvent } from "@/lib/types";

export interface MutexLive {
  kind: "mutex";
  owner: string | null;
  waiters: string[];
}

export interface SemLive {
  kind: "sem";
  count: number;
  waiters: string[];
}

export interface MsgqLive {
  kind: "msgq";
  queue: number[];
  senders: string[];
  receivers: string[];
}

export interface EvflagsLive {
  kind: "evflags";
  flags: number;
  waiters: string[];
}

export type ResourceLive = MutexLive | SemLive | MsgqLive | EvflagsLive;

function num(detail: Record<string, unknown>, key: string): number | null {
  const v = detail[key];
  return typeof v === "number" ? v : null;
}

function str(detail: Record<string, unknown>, key: string): string | null {
  const v = detail[key];
  return typeof v === "string" ? v : null;
}

function bool(detail: Record<string, unknown>, key: string): boolean | null {
  const v = detail[key];
  return typeof v === "boolean" ? v : null;
}

/** live state of one resource at tick t (events must be that resource's, in (t,seq) order) */
export function deriveResourceLive(
  events: readonly TraceEvent[],
  type: string,
  t: number,
  opts: { semInitial?: number; capacity?: number } = {},
): ResourceLive {
  if (type === "sem") {
    let count = opts.semInitial ?? 0;
    const waiters = new Set<string>();
    for (const e of events) {
      if (e.t > t) break;
      if (e.task === null) continue;
      if (e.type === "SEM_WAIT") {
        const acquired = bool(e.detail, "acquired");
        if (acquired === true) {
          count -= 1;
          waiters.delete(e.task);
        } else {
          waiters.add(e.task);
        }
      } else if (e.type === "SEM_SIGNAL") {
        const woken = str(e.detail, "woken");
        if (woken !== null) {
          // hand-off: the token passes directly to the woken waiter
          waiters.delete(woken);
        } else if (bool(e.detail, "overflow") !== true) {
          count += 1;
        }
      }
    }
    return { kind: "sem", count: Math.max(0, count), waiters: [...waiters] };
  }

  if (type === "msgq") {
    const queue: number[] = [];
    const senders: { task: string; msg: number }[] = [];
    const receivers: string[] = [];
    for (const e of events) {
      if (e.t > t) break;
      if (e.task === null) continue;
      const msg = num(e.detail, "msg");
      const blocked = bool(e.detail, "blocked");
      if (e.type === "MSG_SEND") {
        if (blocked === true) {
          senders.push({ task: e.task, msg: msg ?? 0 });
        } else {
          queue.push(msg ?? 0);
          if (receivers.length > 0) {
            // direct hand-off to a blocked receiver
            receivers.shift();
            queue.shift();
          }
        }
      } else if (e.type === "MSG_RECV") {
        if (blocked === true) {
          receivers.push(e.task);
        } else {
          queue.shift();
          if (senders.length > 0) {
            // a freed slot is filled by the oldest blocked sender
            queue.push(senders.shift()?.msg ?? 0);
          }
        }
      }
    }
    return { kind: "msgq", queue, senders: senders.map((s) => s.task), receivers };
  }

  if (type === "evflags") {
    let flags = 0;
    const waiters = new Set<string>();
    for (const e of events) {
      if (e.t > t) break;
      if (e.type === "EV_SET") {
        const mask = num(e.detail, "mask");
        if (mask !== null) flags |= mask;
        const woken = e.detail.woken;
        if (Array.isArray(woken)) {
          for (const w of woken) if (typeof w === "string") waiters.delete(w);
        }
      } else if (e.type === "EV_WAIT") {
        const mask = num(e.detail, "mask");
        const satisfied = bool(e.detail, "satisfied");
        if (e.task !== null && satisfied !== true) waiters.add(e.task);
        // satisfied waits consume the waited bits (standard event-flag semantics)
        if (satisfied === true && mask !== null) flags &= ~mask;
      }
    }
    return { kind: "evflags", flags, waiters: [...waiters] };
  }

  // mutex (default)
  let owner: string | null = null;
  const waiters = new Set<string>();
  for (const e of events) {
    if (e.t > t) break;
    if (e.task === null) continue;
    if (e.type === "LOCK_ACQUIRE") {
      waiters.delete(e.task);
      owner = e.task;
    } else if (e.type === "LOCK_RELEASE") {
      waiters.delete(e.task);
      const handedTo = str(e.detail, "handedTo");
      if (handedTo !== null) {
        waiters.delete(handedTo);
        owner = handedTo;
      } else {
        owner = null;
      }
    } else if (e.type === "LOCK_BLOCK") {
      waiters.add(e.task);
    }
  }
  return { kind: "mutex", owner, waiters: [...waiters] };
}

/** contention markers for the mini timeline strip (one per blocking event) */
export interface ContentionMarker {
  t: number;
  seq: number;
  task: string | null;
  label: string;
}

export function contentionMarkers(events: readonly TraceEvent[], type: string): ContentionMarker[] {
  const markers: ContentionMarker[] = [];
  for (const e of events) {
    let isContention = false;
    switch (type) {
      case "sem":
        isContention = e.type === "SEM_WAIT" && bool(e.detail, "acquired") === false;
        break;
      case "msgq":
        isContention =
          (e.type === "MSG_SEND" || e.type === "MSG_RECV") && bool(e.detail, "blocked") === true;
        break;
      case "evflags":
        isContention = e.type === "EV_WAIT" && bool(e.detail, "satisfied") === false;
        break;
      default:
        isContention = e.type === "LOCK_BLOCK";
    }
    if (isContention) {
      markers.push({
        t: e.t,
        seq: e.seq,
        task: e.task,
        label: `${e.type} t=${e.t}${e.task ? ` · ${e.task}` : ""}`,
      });
    }
  }
  return markers;
}

/** per-resource event index (events mention the resource id in `res`) */
export function eventsByResource(
  trace: readonly TraceEvent[],
  resourceIds: readonly string[],
): Map<string, TraceEvent[]> {
  const map = new Map<string, TraceEvent[]>(resourceIds.map((id) => [id, []]));
  for (const e of trace) {
    if (e.res !== null) {
      const list = map.get(e.res);
      if (list) list.push(e);
    }
  }
  return map;
}
