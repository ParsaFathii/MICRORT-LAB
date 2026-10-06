// result.hpp — MicroRT-Lab result document assembly (schema
// micrort-result/1). buildResultDoc() turns a finished Simulation into the
// full JSON document: config echo + hash, per-task blocks, metrics, trace,
// gantt, resources, deadlocks, memory, anomalies, schedulerNotes. All
// empty arrays are always present; absent scalars are null; averages are
// rounded to 3 decimals.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_RESULT_HPP
#define MICRORT_RESULT_HPP

#include <cstdint>

#include <nlohmann/json.hpp>

namespace micrort {

class Simulation;

/// Assemble the complete result document from a run Simulation. Pure: no
/// simulation state is mutated. `wallMicros` is supplied by the caller
/// (CLI measures the whole run; tests may pass 0).
nlohmann::json buildResultDoc(const Simulation& sim,
                              std::uint64_t wallMicros);

} // namespace micrort

#endif // MICRORT_RESULT_HPP
