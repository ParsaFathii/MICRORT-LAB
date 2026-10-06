"""Analysis layer: Python-side recomputation, RT feasibility, starvation, fragmentation.

Everything here is derived from the engine's *result document* (trace + gantt +
per-task accounting). recompute_metrics() is the schema's mandated cross-check
of the engine's aggregate metrics; rt_analysis() separates the *theoretical*
(Rate-Monotonic / Liu & Layland) feasibility verdicts from the *measured*
simulation outcome.
"""

from __future__ import annotations

import math
from typing import Any

RECOMPUTE_TOLERANCE = 1e-3

# Metrics carried in comparison summaries (name -> "higher"|"lower" is better).
COMPARISON_METRICS: dict[str, str] = {
    "avgWaiting": "lower",
    "avgTurnaround": "lower",
    "avgResponse": "lower",
    "throughput": "higher",
    "cpuUtilization": "higher",
    "ctxSwitches": "lower",
    "preemptions": "lower",
    "deadlineMisses": "lower",
    "completedJobs": "higher",
}


def _mean(xs: list[float]) -> float | None:
    return sum(xs) / len(xs) if xs else None


def _round3(v: float | None) -> float | None:
    return None if v is None else round(v, 3)


# ---------------------------------------------------------------------------
# Metric recomputation (trace + gantt cross-check)
# ---------------------------------------------------------------------------


def _job_records(result: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    """Per-task job records built purely from trace events.

    Releases come from TASK_ARRIVAL / JOB_RELEASE (detail.job), completions
    from TASK_COMPLETE (detail.job), dispatch times from DISPATCH events.
    """
    releases: dict[tuple[str, int], int] = {}
    completions: dict[tuple[str, int], int] = {}
    dispatches: dict[str, list[int]] = {}
    for ev in result.get("trace") or []:
        etype = ev.get("type")
        task = ev.get("task")
        if task is None:
            continue
        job = (ev.get("detail") or {}).get("job")
        if etype in ("TASK_ARRIVAL", "JOB_RELEASE") and job is not None:
            releases.setdefault((task, job), ev.get("t", 0))
        elif etype == "TASK_COMPLETE" and job is not None:
            completions.setdefault((task, job), ev.get("t", 0))
        elif etype == "DISPATCH":
            dispatches.setdefault(task, []).append(ev.get("t", 0))

    per_task: dict[str, list[dict[str, Any]]] = {}
    for (task, job) in sorted(releases, key=lambda k: (k[0], k[1])):
        rec = {
            "task": task,
            "job": job,
            "release": releases[(task, job)],
            "completion": completions.get((task, job)),
        }
        per_task.setdefault(task, []).append(rec)
    for recs in per_task.values():
        recs.sort(key=lambda r: r["release"])
        for i, rec in enumerate(recs):
            nxt = recs[i + 1]["release"] if i + 1 < len(recs) else None
            rec["nextRelease"] = nxt
    for task, times in dispatches.items():
        if task not in per_task:  # dispatch without any traced release
            continue
        for rec in per_task[task]:
            end = rec["completion"]
            if end is None:
                end = rec["nextRelease"] if rec["nextRelease"] is not None else None
            first: int | None = None
            for t in times:
                if t >= rec["release"] and (end is None or t < end):
                    first = t
                    break
            rec["firstDispatch"] = first
    return per_task


def recompute_metrics(result: dict[str, Any]) -> dict[str, Any]:
    """Recompute aggregate metrics from trace + gantt and diff vs the engine.

    Semantics mirrored from the engine (result.cpp / simulation.cpp):
      * waitTimes / turnaroundTimes exist only for COMPLETED jobs
        (pushed at job completion);
      * responseTimes exist for every job that was dispatched at least once;
      * cpuUtilization = sum(RUNNING gantt segments) / simulatedUntil
        (context-switch zones are RUNNING segments of the incoming task);
      * throughput = completedJobs / simulatedUntil.
    """
    sim_until = result.get("simulatedUntil") or 0
    sim_until = max(sim_until, 1)
    gantt = result.get("gantt") or []

    per_task = _job_records(result)

    waits: list[float] = []
    turnarounds: list[float] = []
    responses: list[float] = []
    ready_by_task: dict[str, list[tuple[int, int]]] = {}
    for seg in gantt:
        task, state = seg.get("task"), seg.get("state")
        if task is None:
            continue
        if state == "READY":
            ready_by_task.setdefault(task, []).append((seg["t0"], seg["t1"]))
    for recs in per_task.values():
        for rec in recs:
            if rec["completion"] is None:
                continue  # unfinished jobs contribute no wait/turnaround
            rel, done = rec["release"], rec["completion"]
            waits.append(
                sum(
                    max(0, min(t1, done) - max(t0, rel))
                    for (t0, t1) in ready_by_task.get(rec["task"], [])
                )
            )
            turnarounds.append(done - rel)
            if rec.get("firstDispatch") is not None:
                responses.append(rec["firstDispatch"] - rel)
    for recs in per_task.values():  # dispatched-but-unfinished jobs still count
        for rec in recs:
            if rec["completion"] is None and rec.get("firstDispatch") is not None:
                responses.append(rec["firstDispatch"] - rec["release"])

    busy = sum(seg["t1"] - seg["t0"] for seg in gantt if seg.get("state") == "RUNNING")
    completed = sum(1 for recs in per_task.values() for r in recs if r["completion"] is not None)
    released = sum(len(recs) for recs in per_task.values())

    recomputed = {
        "avgWaiting": _round3(_mean(waits)),
        "avgTurnaround": _round3(_mean(turnarounds)),
        "avgResponse": _round3(_mean(responses)),
        "cpuUtilization": _round3(busy / sim_until),
        "throughput": _round3(completed / sim_until),
        "completedJobs": completed,
        "releasedJobs": released,
    }
    engine = result.get("metrics", {}) or {}
    keys = ["avgWaiting", "avgTurnaround", "avgResponse", "cpuUtilization", "throughput"]
    deltas: dict[str, float | None] = {}
    for key in keys:
        eng, rec = engine.get(key), recomputed.get(key)
        if eng is None or rec is None:
            deltas[key] = None
        else:
            deltas[key] = round(rec - float(eng), 6)
    match = all(d is None or abs(d) <= RECOMPUTE_TOLERANCE for d in deltas.values())
    return {
        "simulatedUntil": sim_until,
        "engine": {k: engine.get(k) for k in [*keys, "completedJobs", "releasedJobs"]},
        "recomputed": recomputed,
        "deltas": deltas,
        "match": match,
        "tolerance": RECOMPUTE_TOLERANCE,
    }


# ---------------------------------------------------------------------------
# Real-time scheduling theory
# ---------------------------------------------------------------------------


def rm_utilization_bound(n: int) -> float:
    """Liu & Layland utilization bound for n periodic tasks: n*(2^(1/n)-1)."""
    if n < 1:
        raise ValueError("n must be >= 1")
    return round(n * (2 ** (1.0 / n) - 1.0), 4)


def _cpu_demand(task_cfg: dict[str, Any]) -> int:
    return sum(s.get("d", 0) for s in task_cfg.get("steps", []) if s.get("op") == "cpu")


def rt_analysis(config: dict[str, Any], result: dict[str, Any]) -> dict[str, Any]:
    """Theoretical (RM / Liu & Layland) vs measured analysis of a run.

    The theoretical block uses the *authored* task parameters (WCET proxy =
    sum of cpu step durations, period, relative deadline); the measured block
    uses the engine result (actual deadline misses, completion, starvation).
    The two are reported separately and never mixed.
    """
    tasks_cfg = [t for t in config.get("tasks", []) if isinstance(t, dict)]
    periodic = [t for t in tasks_cfg if t.get("kind") == "periodic" and t.get("period")]

    theory_tasks: list[dict[str, Any]] = []
    utilizations: list[float] = []
    for t in periodic:
        c = _cpu_demand(t)
        period = t["period"]
        util = c / period
        utilizations.append(util)
        theory_tasks.append(
            {
                "id": t.get("id"),
                "wcet": c,
                "period": period,
                "relativeDeadline": t.get("relativeDeadline"),
                "utilization": round(util, 4),
            }
        )
    total_u = round(sum(utilizations), 4)
    n = len(periodic)
    ll_bound = rm_utilization_bound(n) if n else None
    hyperbolic = round(math.prod(u + 1.0 for u in utilizations), 4) if n else None

    result_tasks = result.get("tasks", []) or []
    metrics = result.get("metrics", {}) or {}
    measured_tasks = [
        {
            "id": t.get("id"),
            "jobsReleased": t.get("jobsReleased", 0),
            "jobsCompleted": t.get("jobsCompleted", 0),
            "deadlineMisses": t.get("deadlineMisses", 0),
            "cpuTime": t.get("cpuTime", 0),
            "starved": bool(t.get("starved")),
        }
        for t in result_tasks
    ]
    starved = [t.get("id") for t in result_tasks if t.get("starved")]

    sched_type = (config.get("scheduler") or {}).get("type")
    theoretical = {
        "periodicTaskCount": n,
        "tasks": theory_tasks,
        "totalUtilization": total_u,
        "liuLaylandBound": ll_bound,
        "liuLaylandSchedulable": (ll_bound is not None and total_u <= ll_bound),
        "hyperbolicBound": (
            {
                "product": hyperbolic,
                "limit": 2.0,
                "schedulable": hyperbolic is not None and hyperbolic <= 2.0,
            }
            if hyperbolic is not None
            else None
        ),
        "boundAppliesTo": "rm",
        "note": (
            "Liu & Layland utilization test is sufficient (not necessary) and "
            "assumes Rate-Monotonic priorities with implicit deadlines; it is "
            "shown as a reference bound regardless of the configured scheduler."
            if sched_type != "rm"
            else "Liu & Layland utilization test is sufficient (not necessary) "
            "for implicit-deadline RM task sets."
        ),
    }
    measured = {
        "scheduler": sched_type,
        "simulatedUntil": result.get("simulatedUntil"),
        "deadlineMisses": metrics.get("deadlineMisses", 0),
        "perTask": measured_tasks,
        "completedJobs": metrics.get("completedJobs", 0),
        "releasedJobs": metrics.get("releasedJobs", 0),
        "completionRate": metrics.get("completionRate"),
        "cpuUtilization": metrics.get("cpuUtilization"),
        "starvedTasks": starved,
    }
    return {
        "theoretical": theoretical,
        "measured": measured,
        "verdict": _verdict(total_u, ll_bound, metrics),
    }


def _verdict(total_u: float, ll_bound: float | None, metrics: dict[str, Any]) -> str:
    misses = metrics.get("deadlineMisses", 0)
    if misses:
        return f"measured {misses} deadline miss(es) — task set is NOT schedulable as run"
    if ll_bound is not None and total_u <= ll_bound:
        return (
            "measured 0 misses and total utilization is below the Liu & Layland "
            "sufficient bound — schedulable"
        )
    if ll_bound is not None and total_u > 1.0:
        return "total utilization exceeds 1.0 — task set is infeasible"
    return (
        "measured 0 misses (utilization above the sufficient bound — feasible "
        "here, but the bound gives no guarantee)"
    )


# ---------------------------------------------------------------------------
# Starvation & fragmentation
# ---------------------------------------------------------------------------


def starvation_report(
    result: dict[str, Any], threshold: int = 100, cpu_ratio: float = 5.0
) -> dict[str, Any]:
    """Starvation analysis (engine heuristic re-applied with a configurable threshold).

    Engine heuristic: readyWait >= threshold AND (cpuTime == 0 OR
    readyWait >= cpu_ratio * cpuTime) while some other task ran.
    """
    tasks = result.get("tasks", []) or []
    any_ran = any((t.get("cpuTime") or 0) > 0 for t in tasks)
    per_task = []
    starved = []
    max_wait: dict[str, Any] | None = None
    for t in tasks:
        wait = t.get("readyWaitTotal", 0) or 0
        cpu = t.get("cpuTime", 0) or 0
        recomputed_starved = bool(
            any_ran
            and wait >= threshold
            and (cpu == 0 or wait >= cpu_ratio * cpu)
        )
        entry = {
            "id": t.get("id"),
            "readyWaitTotal": wait,
            "cpuTime": cpu,
            "waitToCpuRatio": round(wait / cpu, 3) if cpu else None,
            "jobsReleased": t.get("jobsReleased", 0),
            "jobsCompleted": t.get("jobsCompleted", 0),
            "engineStarved": bool(t.get("starved")),
            "recomputedStarved": recomputed_starved,
        }
        per_task.append(entry)
        if recomputed_starved:
            starved.append(entry)
        if max_wait is None or wait > max_wait["readyWaitTotal"]:
            max_wait = {"task": t.get("id"), "readyWaitTotal": wait}
    return {
        "threshold": threshold,
        "cpuRatio": cpu_ratio,
        "heuristic": (
            "readyWait >= threshold AND (cpuTime == 0 OR readyWait >= cpuRatio * cpuTime)"
        ),
        "starvedTasks": starved,
        "starvedCount": len(starved),
        "maxReadyWait": max_wait,
        "perTask": per_task,
    }


def fragmentation_summary(result: dict[str, Any]) -> dict[str, Any]:
    """Memory allocator outcome summary (region/pool model)."""
    mem = result.get("memory") or {}
    if not mem or mem.get("model") in (None, "none"):
        return {"enabled": False}
    events = mem.get("events") or []
    layout = mem.get("finalLayout") or []
    free_blocks = [b for b in layout if not b.get("owner")]
    free_total = sum(b.get("size", 0) for b in free_blocks)
    largest = max((b.get("size", 0) for b in free_blocks), default=0)
    metrics = result.get("metrics", {}) or {}
    return {
        "enabled": True,
        "model": mem.get("model"),
        "policy": mem.get("policy"),
        "total": mem.get("total"),
        "eventCount": len(events),
        "peakUsage": mem.get("peakUsage"),
        "allocFailures": metrics.get("allocFailures", 0),
        "failureCount": len(mem.get("failures") or []),
        "leakCount": len(mem.get("leaks") or []),
        "fragmentationAvg": metrics.get("fragmentationAvg"),
        "fragmentationMax": metrics.get("fragmentationMax"),
        "finalFree": {
            "free": free_total,
            "largest": largest,
            "fragmentation": free_total - largest,
        },
    }


# ---------------------------------------------------------------------------
# Comparison metric extraction
# ---------------------------------------------------------------------------


def variant_metrics(result: dict[str, Any]) -> dict[str, Any]:
    """Extract the comparison metric subset from one engine result document."""
    m = result.get("metrics", {}) or {}
    out: dict[str, Any] = {}
    for key in COMPARISON_METRICS:
        out[key] = m.get(key)
    out["simulatedUntil"] = result.get("simulatedUntil")
    return out


__all__ = [
    "COMPARISON_METRICS",
    "RECOMPUTE_TOLERANCE",
    "fragmentation_summary",
    "recompute_metrics",
    "rm_utilization_bound",
    "rt_analysis",
    "starvation_report",
    "variant_metrics",
]
