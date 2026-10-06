// trace.cpp — TraceRecorder implementation.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#include <micrort/trace.hpp>

namespace micrort {

TraceRecorder::TraceRecorder(std::size_t maxEvents)
    : cap_(maxEvents == 0u ? 1u : maxEvents) {}

bool TraceRecorder::record(mrt_time_t t, const char* type,
                           std::int32_t taskIdx, std::int32_t resIdx,
                           const char* from, const char* to, std::int64_t dur,
                           nlohmann::json detail) {
    if (records_.size() >= cap_) {
        return false;
    }
    TraceRecord r;
    r.seq = nextSeq_++;
    r.t = t;
    r.type = type;
    r.taskIdx = taskIdx;
    r.resIdx = resIdx;
    r.from = from;
    r.to = to;
    r.dur = dur;
    r.detail = detail.is_object() ? std::move(detail)
                                  : nlohmann::json::object();
    records_.push_back(std::move(r));
    return true;
}

nlohmann::json TraceRecorder::toJson(
    const std::vector<std::string>& taskIds,
    const std::vector<std::string>& resIds) const {
    using nlohmann::json;
    json arr = json::array();
    for (const TraceRecord& r : records_) {
        json j;
        j["seq"] = r.seq;
        j["t"] = r.t;
        j["type"] = r.type != nullptr ? json(r.type) : json();
        const bool haveTask =
            r.taskIdx >= 0 &&
            static_cast<std::size_t>(r.taskIdx) < taskIds.size();
        const bool haveRes =
            r.resIdx >= 0 && static_cast<std::size_t>(r.resIdx) < resIds.size();
        j["task"] = haveTask ? json(taskIds[static_cast<std::size_t>(r.taskIdx)])
                             : json();
        j["res"] = haveRes ? json(resIds[static_cast<std::size_t>(r.resIdx)])
                           : json();
        j["from"] = r.from != nullptr ? json(r.from) : json();
        j["to"] = r.to != nullptr ? json(r.to) : json();
        j["dur"] = r.dur >= 0 ? json(r.dur) : json();
        j["detail"] = r.detail;
        arr.push_back(std::move(j));
    }
    return arr;
}

} // namespace micrort
