// trace.hpp — MicroRT-Lab deterministic trace recorder.
//
// Every simulation state change becomes a TraceRecord with the exact shape
// of SIMULATION_SCHEMA.md §Trace events:
//     { "seq", "t", "type", "task", "res", "from", "to", "dur", "detail" }
// `seq` is global and strictly increasing; records are produced in
// simulation order, so the array is always sorted by (t, seq).
//
// Determinism contract:
//  - `type`, `from`, `to` are pointers to STATIC string literals (never
//    heap-formatted per record) — byte-identical output across runs.
//  - `detail` is a plain nlohmann object (ordered map => sorted keys).
//  - The recorder is capped (default 500000 events, the schema's total
//    trace ceiling). record() returns false once full and drops further
//    events; the simulation loop treats a full recorder as a stop
//    condition (status "stopped").
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_TRACE_HPP
#define MICRORT_TRACE_HPP

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include <micrort/mrt_types.h>   // mrt_time_t

namespace micrort {

/// One trace record. `taskIdx` / `resIdx` are engine declaration indices
/// (-1 = null); toJson() maps them to the id strings of the schema.
struct TraceRecord {
    std::uint64_t seq = 0;        ///< global, strictly increasing
    mrt_time_t t = 0;             ///< simulation tick
    const char* type = nullptr;   ///< static event-type string (schema §Trace events)
    std::int32_t taskIdx = -1;    ///< -1 = null
    std::int32_t resIdx = -1;     ///< -1 = null
    const char* from = nullptr;   ///< task state name before (or nullptr)
    const char* to = nullptr;     ///< task state name after (or nullptr)
    std::int64_t dur = -1;        ///< burst length when applicable; -1 = null
    nlohmann::json detail = nlohmann::json::object();
};

/// Schema trace-event type strings (single source of truth, static storage).
namespace trace_event {
inline constexpr const char* SIM_START       = "SIM_START";
inline constexpr const char* SIM_END         = "SIM_END";
inline constexpr const char* TASK_ARRIVAL    = "TASK_ARRIVAL";
inline constexpr const char* JOB_RELEASE     = "JOB_RELEASE";
inline constexpr const char* DISPATCH        = "DISPATCH";
inline constexpr const char* PREEMPT         = "PREEMPT";
inline constexpr const char* QUANTUM_EXPIRE  = "QUANTUM_EXPIRE";
inline constexpr const char* CONTEXT_SWITCH  = "CONTEXT_SWITCH";
inline constexpr const char* CPU_START       = "CPU_START";
inline constexpr const char* CPU_END         = "CPU_END";
inline constexpr const char* IO_START        = "IO_START";
inline constexpr const char* IO_END          = "IO_END";
inline constexpr const char* SLEEP_START     = "SLEEP_START";
inline constexpr const char* SLEEP_END       = "SLEEP_END";
inline constexpr const char* LOCK_ACQUIRE    = "LOCK_ACQUIRE";
inline constexpr const char* LOCK_BLOCK      = "LOCK_BLOCK";
inline constexpr const char* LOCK_RELEASE    = "LOCK_RELEASE";
inline constexpr const char* LOCK_INHERIT    = "LOCK_INHERIT";
inline constexpr const char* LOCK_UNINHERIT  = "LOCK_UNINHERIT";
inline constexpr const char* SEM_WAIT        = "SEM_WAIT";
inline constexpr const char* SEM_SIGNAL      = "SEM_SIGNAL";
inline constexpr const char* MSG_SEND        = "MSG_SEND";
inline constexpr const char* MSG_RECV        = "MSG_RECV";
inline constexpr const char* EV_WAIT         = "EV_WAIT";
inline constexpr const char* EV_SET          = "EV_SET";
inline constexpr const char* MEM_ALLOC       = "MEM_ALLOC";
inline constexpr const char* MEM_FREE        = "MEM_FREE";
inline constexpr const char* DEADLOCK        = "DEADLOCK";
inline constexpr const char* DEADLINE_MISS   = "DEADLINE_MISS";
inline constexpr const char* TASK_COMPLETE   = "TASK_COMPLETE";
inline constexpr const char* AGING_BOOST     = "AGING_BOOST";
inline constexpr const char* TIMER_EXPIRE    = "TIMER_EXPIRE";
} // namespace trace_event

/// Task-state names for the trace `from`/`to` fields (schema §State
/// semantics; UNUSED is not a reported state — use nullptr instead).
namespace trace_state {
inline constexpr const char* READY      = "READY";
inline constexpr const char* RUNNING    = "RUNNING";
inline constexpr const char* BLOCKED    = "BLOCKED";
inline constexpr const char* WAITING    = "WAITING";
inline constexpr const char* SLEEPING   = "SLEEPING";
inline constexpr const char* TERMINATED = "TERMINATED";
} // namespace trace_state

/// Append-only recorder. NOT thread-safe (the simulation is single
/// threaded by design). No unordered containers anywhere.
class TraceRecorder {
public:
    /// Schema ceiling for total trace events.
    static constexpr std::size_t kMaxEvents = 500000u;

    explicit TraceRecorder(std::size_t maxEvents = kMaxEvents);

    /// Append one record. `detail` may be an empty object. Returns false
    /// (and records nothing) when the cap was already reached. `seq` is
    /// assigned here — callers never supply it.
    bool record(mrt_time_t t, const char* type,
                std::int32_t taskIdx, std::int32_t resIdx,
                const char* from, const char* to, std::int64_t dur,
                nlohmann::json detail);

    std::size_t size() const { return records_.size(); }
    bool full() const { return records_.size() >= cap_; }
    std::uint64_t nextSeq() const { return nextSeq_; }

    /// Schema-shaped array: id strings for task/res (null when -1).
    nlohmann::json toJson(const std::vector<std::string>& taskIds,
                          const std::vector<std::string>& resIds) const;

private:
    std::size_t cap_;
    std::uint64_t nextSeq_ = 0;
    std::vector<TraceRecord> records_;
};

} // namespace micrort

#endif // MICRORT_TRACE_HPP
