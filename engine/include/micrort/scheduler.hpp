// scheduler.hpp — MicroRT-Lab dispatch policy abstraction (8 schedulers).
//
// Division of labour with the kernel ready queue (mrt_queue.c):
//   The kernel ready queue ALWAYS ranks by (effective priority DESC,
//   enqueue_seq ASC). Schedulers differ in HOW tasks are ranked and WHEN
//   preemption happens:
//
//   * ranksByQueue() == true  (fifo, rr, priority, priority_p):
//       the kernel queue order IS the dispatch order — the engine simply
//       pops the head. compare() mirrors the queue order for completeness
//       and testing (fifo/rr: seq; priority/priority_p: effective DESC,
//       seq).
//
//   * ranksByQueue() == false (sjf, srtf, rm, edf):
//       the engine scans the ready queue and dispatches the candidate for
//       which compare() returns the smallest rank. The kernel queue remains
//       the bookkeeping structure; only the selection rule differs.
//
//   * preemptive() == true (rr, priority_p, srtf, rm, edf):
//       a state change (arrival / unblock / burst end / quantum expiry)
//       triggers a preemption check — the CPU switches only when a
//       STRICTLY more urgent candidate exists (schema: no thrash on ties).
//       non-preemptive schedulers run the current task to its next block
//       point (fifo, priority, sjf).
//
//   * quantum() > 0 (rr only): rotate the running task back to READY when
//       its quantum expires; 0 = no quantum.
//
// RM <-> kernel priority mapping (stage-2 contract, schema §Determinism 8):
//   RM is static: the engine assigns each task's BASE priority from its
//   period rank so the kernel queue order equals RM order:
//       rank r    = index of the task in ascending-period order
//                   (aperiodic / period==0 tasks rank LAST, ties keep
//                   declaration order)
//       base_prio = MRT_PRIO_MAX - r            (>= 1 with <= 64 tasks)
//       aperiodic tasks -> base_prio = MRT_PRIO_MIN
//   Aging must stay OFF under RM (it would corrupt the static mapping).
//   compare() independently implements "period ASC, tie seq ASC" so this
//   scheduler is correct standalone even without the priority mapping.
//
// EDF (schema §Determinism 8): earlier absolute deadline wins; no deadline
//   => MRT_TIME_MAX (ranks last); ties -> FIFO (seq).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_SCHEDULER_HPP
#define MICRORT_SCHEDULER_HPP

#include <cstdint>
#include <memory>
#include <string>

#include <micrort/config.hpp>   // SchedType, SchedulerCfg
#include <micrort/mrt_types.h>  // mrt_task_id_t, mrt_prio_t, mrt_seq_t, mrt_time_t

namespace micrort {

/// Sentinel period for aperiodic / period-less candidates under RM: they
/// rank LAST (schema: "aperiodic tasks under rm get priority MRT_PRIO_MIN").
/// compare() also maps a raw period of 0 to this sentinel defensively.
inline constexpr mrt_time_t kSchedAperiodicPeriod = MRT_TIME_MAX;

/// One dispatch decision input, snapshotted from the kernel TCB + engine
/// job state at a scheduling point. The engine fills a candidate per READY
/// task; schedulers rank them via compare().
struct SchedCandidate {
    mrt_task_id_t id = MRT_TASK_ID_NONE;      ///< kernel handle; final tie-break
    mrt_prio_t effective = 0;                 ///< effective priority (aging/inherit incl.)
    mrt_seq_t seq = 0;                        ///< kernel enqueue_seq; FIFO tie-break
    std::uint64_t remainingCpu = 0;           ///< current job remaining CPU demand (SJF/SRTF)
    mrt_time_t absoluteDeadline = MRT_TIME_MAX; ///< release + relativeDeadline; MRT_TIME_MAX = none (EDF)
    mrt_time_t period = kSchedAperiodicPeriod;  ///< task period; aperiodic sentinel (RM)
};

/// Dispatch policy. Implementations are stateless value objects: the
/// engine owns all simulation state; compare() is pure.
class IScheduler {
public:
    virtual ~IScheduler() = default;

    /// Which of the 8 policies this object implements.
    virtual SchedType type() const = 0;

    /// true when preemption checks run at every dispatch-relevant state
    /// change (rr, priority_p, srtf, rm, edf).
    virtual bool preemptive() const = 0;

    /// true when the kernel ready queue head IS the next dispatch
    /// (fifo, rr, priority, priority_p); false when the engine must scan
    /// the queue and rank candidates with compare() (sjf, srtf, rm, edf).
    virtual bool ranksByQueue() const = 0;

    /// Ordering over two candidates: <0 when `a` dispatches before `b`,
    /// 0 when identical, >0 otherwise. Pure, total (id as final tie-break).
    virtual int compare(const SchedCandidate& a, const SchedCandidate& b) const = 0;

    /// RR time slice; 0 = no quantum (every type except rr).
    virtual std::uint32_t quantum() const = 0;
};

/// Factory. `cfg` must be validated (SchedulerCfg from a parsed config):
/// rr quantum outside 1..1000 falls back to the documented default 4.
/// Never returns null. Unreachable enum values fall back to FIFO.
std::unique_ptr<IScheduler> makeScheduler(const SchedulerCfg& cfg);

/// "fifo" | "rr" | "priority" | "priority_p" | "sjf" | "srtf" | "rm" | "edf".
const char* schedTypeName(SchedType t);

/// Inverse of schedTypeName; returns false for unknown strings (out
/// untouched). Used by the config parser.
bool schedTypeFromString(const std::string& s, SchedType& out);

} // namespace micrort

#endif // MICRORT_SCHEDULER_HPP
