"use client";

/**
 * MicroRT-Lab — shared event-type color categories.
 *
 * Maps every trace event type onto a workstation color token so the
 * inspector badges, live console lines, legend and markers agree.
 * Categories: accent (running/dispatch), ctx (overhead), ok (success),
 * bad (danger), waiting (I/O), sleeping (sleep), ready (runnable),
 * blocked (resource wait), dim (neutral).
 */

export type EventCategory =
  | "accent"
  | "ctx"
  | "ok"
  | "bad"
  | "waiting"
  | "sleeping"
  | "ready"
  | "blocked"
  | "dim";

export function eventCategory(type: string): EventCategory {
  switch (type) {
    case "DISPATCH":
    case "CPU_START":
    case "CPU_END":
      return "accent";
    case "CONTEXT_SWITCH":
    case "QUANTUM_EXPIRE":
    case "LOCK_INHERIT":
    case "LOCK_UNINHERIT":
    case "AGING_BOOST":
      return "ctx";
    case "PREEMPT":
    case "LOCK_BLOCK":
    case "DEADLINE_MISS":
    case "DEADLOCK":
      return "bad";
    case "TASK_COMPLETE":
    case "LOCK_ACQUIRE":
    case "LOCK_RELEASE":
    case "SEM_SIGNAL":
    case "EV_SET":
      return "ok";
    case "IO_START":
    case "IO_END":
      return "waiting";
    case "SLEEP_START":
    case "SLEEP_END":
      return "sleeping";
    case "TASK_ARRIVAL":
    case "JOB_RELEASE":
      return "ready";
    case "SEM_WAIT":
    case "MSG_SEND":
    case "MSG_RECV":
    case "EV_WAIT":
      return "blocked";
    default:
      return "dim";
  }
}

/** text-* color class per category */
export const CATEGORY_TEXT: Record<EventCategory, string> = {
  accent: "text-accent",
  ctx: "text-ctx",
  ok: "text-ok",
  bad: "text-bad",
  waiting: "text-state-waiting",
  sleeping: "text-state-sleeping",
  ready: "text-state-ready",
  blocked: "text-state-blocked",
  dim: "text-ink-dim",
};

/** subtle chip classes per category (border + tinted bg + text) */
export const CATEGORY_CHIP: Record<EventCategory, string> = {
  accent: "border-accent/40 bg-accent/10 text-accent",
  ctx: "border-ctx/60 bg-ctx/10 text-ink",
  ok: "border-ok/40 bg-ok/10 text-ok",
  bad: "border-bad/40 bg-bad/10 text-bad",
  waiting: "border-state-waiting/40 bg-state-waiting/10 text-state-waiting",
  sleeping: "border-state-sleeping/40 bg-state-sleeping/10 text-state-sleeping",
  ready: "border-line bg-state-ready/15 text-ink-dim",
  blocked: "border-state-blocked/40 bg-state-blocked/10 text-state-blocked",
  dim: "border-line bg-muted text-ink-dim",
};
