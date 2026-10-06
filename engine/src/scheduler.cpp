// scheduler.cpp — the 8 dispatch policies behind IScheduler.
//
// Ordering rules (SIMULATION_SCHEMA.md §Determinism & tie-breaking):
//   fifo / rr        : queue order  (compare: seq ASC, then id)
//   priority/_p      : effective priority DESC, seq ASC (queue mirrors it)
//   sjf / srtf       : remaining CPU of the current job ASC, seq ASC
//   rm               : period ASC (aperiodic/0 -> MRT_TIME_MAX sentinel),
//                      seq ASC — static, mapped onto kernel base priorities
//   edf              : absolute deadline ASC (none -> MRT_TIME_MAX), seq ASC
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <micrort/scheduler.hpp>

namespace micrort {
namespace {

// ---------------------------------------------------------------------
// Queue-order schedulers (fifo, rr, priority, priority_p)
// ---------------------------------------------------------------------

class FifoSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::FIFO; }
    bool preemptive() const override { return false; }
    bool ranksByQueue() const override { return true; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

class RRSched final : public IScheduler {
public:
    explicit RRSched(std::uint32_t q) : quantum_(q) {}
    SchedType type() const override { return SchedType::RR; }
    bool preemptive() const override { return true; }
    bool ranksByQueue() const override { return true; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return quantum_; }

private:
    std::uint32_t quantum_; // 1..1000 (validated; default 4)
};

class PrioritySched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::Priority; }
    bool preemptive() const override { return false; }
    bool ranksByQueue() const override { return true; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

class PriorityPSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::PriorityP; }
    bool preemptive() const override { return true; }
    bool ranksByQueue() const override { return true; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

// ---------------------------------------------------------------------
// Scan-and-rank schedulers (sjf, srtf, rm, edf)
// ---------------------------------------------------------------------

class SJFSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::SJF; }
    bool preemptive() const override { return false; }
    bool ranksByQueue() const override { return false; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

class SRTFSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::SRTF; }
    bool preemptive() const override { return true; }
    bool ranksByQueue() const override { return false; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

class RMSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::RM; }
    bool preemptive() const override { return true; }
    bool ranksByQueue() const override { return false; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

class EDFSched final : public IScheduler {
public:
    SchedType type() const override { return SchedType::EDF; }
    bool preemptive() const override { return true; }
    bool ranksByQueue() const override { return false; }
    int compare(const SchedCandidate& a, const SchedCandidate& b) const override;
    std::uint32_t quantum() const override { return 0; }
};

// ---------------------------------------------------------------------
// compare() implementations
// ---------------------------------------------------------------------

int FifoSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    // Pure queue order: enqueue sequence, id only as an impossible-in-
    // practice final guard (seq is unique per enqueue).
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

int RRSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    // Same ordering as FIFO; the quantum adds the rotation, not the rank.
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

int PrioritySched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    if (a.effective != b.effective) return a.effective > b.effective ? -1 : 1;
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

int PriorityPSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    return PrioritySched{}.compare(a, b); // same rank, preemption differs
}

int SJFSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    if (a.remainingCpu != b.remainingCpu) {
        return a.remainingCpu < b.remainingCpu ? -1 : 1;
    }
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

int SRTFSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    return SJFSched{}.compare(a, b); // same rank, preemption differs
}

int RMSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    // Shorter period = more urgent; aperiodic (or raw 0) maps to the
    // MRT_TIME_MAX sentinel and ranks last. Ties -> FIFO (seq).
    const mrt_time_t pa = (a.period == 0) ? kSchedAperiodicPeriod : a.period;
    const mrt_time_t pb = (b.period == 0) ? kSchedAperiodicPeriod : b.period;
    if (pa != pb) return pa < pb ? -1 : 1;
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

int EDFSched::compare(const SchedCandidate& a, const SchedCandidate& b) const {
    // Earlier absolute deadline wins; MRT_TIME_MAX (no deadline) last.
    if (a.absoluteDeadline != b.absoluteDeadline) {
        return a.absoluteDeadline < b.absoluteDeadline ? -1 : 1;
    }
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    if (a.id != b.id) return a.id < b.id ? -1 : 1;
    return 0;
}

} // namespace

// ---------------------------------------------------------------------
// Public factory + name mapping
// ---------------------------------------------------------------------

std::unique_ptr<IScheduler> makeScheduler(const SchedulerCfg& cfg) {
    switch (cfg.type) {
    case SchedType::FIFO:
        return std::make_unique<FifoSched>();
    case SchedType::RR: {
        // Validated configs carry 1..1000; anything else falls back to the
        // documented default quantum of 4 (defensive, keeps stage 2 simple).
        const std::int64_t q = cfg.quantum;
        const std::uint32_t quantum =
            (q >= 1 && q <= 1000) ? static_cast<std::uint32_t>(q) : 4u;
        return std::make_unique<RRSched>(quantum);
    }
    case SchedType::Priority:
        return std::make_unique<PrioritySched>();
    case SchedType::PriorityP:
        return std::make_unique<PriorityPSched>();
    case SchedType::SJF:
        return std::make_unique<SJFSched>();
    case SchedType::SRTF:
        return std::make_unique<SRTFSched>();
    case SchedType::RM:
        return std::make_unique<RMSched>();
    case SchedType::EDF:
        return std::make_unique<EDFSched>();
    }
    // Unreachable for any value of the closed enum; safe fallback.
    return std::make_unique<FifoSched>();
}

const char* schedTypeName(SchedType t) {
    switch (t) {
    case SchedType::FIFO: return "fifo";
    case SchedType::RR: return "rr";
    case SchedType::Priority: return "priority";
    case SchedType::PriorityP: return "priority_p";
    case SchedType::SJF: return "sjf";
    case SchedType::SRTF: return "srtf";
    case SchedType::RM: return "rm";
    case SchedType::EDF: return "edf";
    }
    return "fifo"; // unreachable for valid enum values
}

bool schedTypeFromString(const std::string& s, SchedType& out) {
    if (s == "fifo") { out = SchedType::FIFO; return true; }
    if (s == "rr") { out = SchedType::RR; return true; }
    if (s == "priority") { out = SchedType::Priority; return true; }
    if (s == "priority_p") { out = SchedType::PriorityP; return true; }
    if (s == "sjf") { out = SchedType::SJF; return true; }
    if (s == "srtf") { out = SchedType::SRTF; return true; }
    if (s == "rm") { out = SchedType::RM; return true; }
    if (s == "edf") { out = SchedType::EDF; return true; }
    return false;
}

} // namespace micrort
