// config.hpp — MicroRT-Lab experiment configuration model, JSON parser,
// validator and canonical serializer.
//
// This is the C++ mirror of docs/spec/SIMULATION_SCHEMA.md §Experiment
// configuration (THE binding data contract). Stage-2 of the engine consumes
// these structs directly; parse+validate collect ALL problems (never throw,
// never abort after the first error).
//
// Ordering contract: `Config::tasks` and `Config::resources` PRESERVE the
// declaration order of the source JSON (vector, never map) — event ordering
// at a tick and RM tie-breaks depend on it.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_CONFIG_HPP
#define MICRORT_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace micrort {

// ---------------------------------------------------------------------
// Enumerations (string values in JSON per SIMULATION_SCHEMA.md)
// ---------------------------------------------------------------------

/// Program step op codes. JSON strings: cpu, io, sleep, lock, unlock, wait,
/// signal, send, recv, evwait, evset, alloc, free.
enum class StepOp {
    CPU, IO, SLEEP, LOCK, UNLOCK, WAIT, SIGNAL, SEND, RECV,
    EVWAIT, EVSET, ALLOC, FREE
};

/// JSON: aperiodic | periodic | sporadic.
enum class TaskKind { Aperiodic, Periodic, Sporadic };

/// JSON: mutex | sem | msgq | evflags.
enum class ResKind { Mutex, Sem, MsgQ, EvFlags };

/// JSON: none | inherit (mutex only).
enum class ResProtocol { None, Inherit };

/// JSON: region | pool.
enum class MemModel { Region, Pool };

/// JSON: first_fit | best_fit | worst_fit (region model only).
enum class MemPolicy { First, Best, Worst };

/// JSON: fifo | rr | priority | priority_p | sjf | srtf | rm | edf.
/// (Declared here because SchedulerCfg needs it; scheduler.hpp builds the
/// dispatch policy classes on top of this enum.)
enum class SchedType { FIFO, RR, Priority, PriorityP, SJF, SRTF, RM, EDF };

// ---------------------------------------------------------------------
// Enum <-> string helpers (all return false on unknown values)
// ---------------------------------------------------------------------

const char* stepOpName(StepOp op);        // "cpu" ... "free"
bool stepOpFromString(const std::string& s, StepOp& out);
const char* taskKindName(TaskKind k);     // "aperiodic" | "periodic" | "sporadic"
bool taskKindFromString(const std::string& s, TaskKind& out);
const char* resKindName(ResKind k);       // "mutex" | "sem" | "msgq" | "evflags"
bool resKindFromString(const std::string& s, ResKind& out);
const char* resProtocolName(ResProtocol p); // "none" | "inherit"
bool resProtocolFromString(const std::string& s, ResProtocol& out);
const char* memModelName(MemModel m);     // "region" | "pool"
bool memModelFromString(const std::string& s, MemModel& out);
const char* memPolicyName(MemPolicy p);   // "first_fit" | "best_fit" | "worst_fit"
bool memPolicyFromString(const std::string& s, MemPolicy& out);

// ---------------------------------------------------------------------
// Configuration structs (defaults documented per field)
// ---------------------------------------------------------------------

/// One program step of a job. Only the fields relevant to `op` are used;
/// the parser leaves defaults for the rest (stage 2 reads only the fields
/// the schema table assigns to the op).
struct StepCfg {
    StepOp op = StepOp::CPU;      ///< operation kind
    std::int64_t d = 0;           ///< burst length for cpu/io/sleep (ticks, 1..100000)
    std::string res;              ///< resource id for lock/unlock/wait/signal/send/recv/evwait/evset
    std::string dev;              ///< optional io device label ("" = none)
    std::int64_t msg = 0;         ///< send payload (int32 range enforced by validation)
    std::uint32_t mask = 0;       ///< evwait/evset bitmask (0..0xFFFFFFFF)
    bool modeAll = false;         ///< evwait mode: false = "any", true = "all"
    std::int64_t size = 0;        ///< alloc size in bytes (>= 1)
    std::string tag;              ///< alloc tag (default "a<stepIndex>") / free target tag (REQUIRED)
};

/// One schedulable entity (SIMULATION_SCHEMA.md §Task object).
struct TaskCfg {
    std::string id;               ///< unique, 1..31 chars [A-Za-z0-9_-]
    std::string name;             ///< display name; defaults to id
    TaskKind kind = TaskKind::Aperiodic;
    std::int32_t priority = 0;    ///< base priority, -100..100, higher = more urgent
    std::int64_t arrival = 0;     ///< first release tick, >= 0 (default 0)
    std::int64_t period = 0;      ///< periodic only, >= 1 (0 = unset)
    std::int64_t relativeDeadline = 0; ///< ticks from job release; 0 = no deadline
    std::int64_t jitter = 0;      ///< periodic release jitter, 0..period-1
    std::vector<std::int64_t> releases; ///< sporadic only: explicit ascending release ticks
    std::vector<StepCfg> steps;   ///< job program, 1..256 steps
};

/// One synchronization resource (SIMULATION_SCHEMA.md §Resource object).
struct ResCfg {
    std::string id;               ///< unique, 1..31 chars [A-Za-z0-9_-]
    ResKind kind = ResKind::Mutex;
    ResProtocol protocol = ResProtocol::None; ///< mutex only (default none)
    std::int64_t initial = 0;     ///< sem: initial count, 0..max (default 0)
    std::int64_t max = 0;         ///< sem: max count, >= 1 (required for sem)
    std::int64_t capacity = 0;    ///< msgq: capacity, >= 1 (required for msgq)
};

/// Memory model block; `enabled == false` (block absent) disables alloc/free.
struct MemoryCfg {
    bool enabled = false;
    MemModel model = MemModel::Region;       ///< region | pool
    std::int64_t total = 0;                  ///< region: total bytes (>= 1);
                                             ///< pool: blockSize * blockCount
    MemPolicy policy = MemPolicy::First;     ///< region only (default first_fit)
    std::int64_t blockSize = 0;              ///< pool only (>= 1)
    std::int64_t blockCount = 0;             ///< pool only (>= 1)
};

/// Aging block; absent => disabled (no effect). Effective only for the
/// priority*/rr ready-queue ranking (see schema §aging).
struct AgingCfg {
    bool enabled = false;
    std::int64_t interval = 0;    ///< +1 effective prio every N ticks in READY (>= 1)
    std::int64_t cap = 0;         ///< max boost above base priority (>= 0)
};

/// Scheduler selection. `quantum` is RR only: parsed default 4 when the rr
/// block omits it; 0 = "not applicable" for every other type.
struct SchedulerCfg {
    SchedType type = SchedType::FIFO;
    std::int64_t quantum = 0;     ///< rr: 1..1000; other types: 0
};

/// Full experiment configuration (root object of micrort-config/1).
struct Config {
    std::string schema;           ///< must be "micrort-config/1"
    std::string name;             ///< experiment name, non-empty
    std::string description;      ///< optional, "" when absent
    std::uint64_t seed = 0;       ///< PRNG seed (jitter); 0 = static schedule
    std::int64_t duration = 0;    ///< horizon in ticks, 1..100000
    std::int64_t cpus = 1;        ///< must be 1 in v1
    std::int64_t contextSwitchCost = 0; ///< ticks per CPU handover, 0..10
    SchedulerCfg scheduler;
    AgingCfg aging;
    MemoryCfg memory;
    std::vector<TaskCfg> tasks;       ///< declaration order preserved
    std::vector<ResCfg> resources;    ///< declaration order preserved
};

// ---------------------------------------------------------------------
// Parse / validate / serialize
// ---------------------------------------------------------------------

/// Parse a DOM previously produced by nlohmann (e.g. json::parse). Collects
/// EVERY problem into `errors` (path-prefixed, e.g.
/// "tasks[2].steps[0].op: unknown op 'spin'") and never throws. Missing
/// required fields, wrong field types and unknown enum strings are parse
/// errors here; cross-field semantic rules live in validateConfig().
/// `out` is reset to defaults first and holds everything parsed so far
/// (usable only when the return value is true). Returns true iff no errors.
bool parseConfig(const nlohmann::json& j, Config& out,
                 std::vector<std::string>& errors);

/// Convenience wrapper: json::parse(text) with parse errors caught and
/// reported as "json: malformed JSON: <parser message>" in `errors` (the
/// task contract: malformed JSON is a validation failure, exit code 2 —
/// no exception ever escapes). Returns parseConfig() of the DOM.
bool parseConfigText(const std::string& text, Config& out,
                     std::vector<std::string>& errors);

/// Enforce every rule of SIMULATION_SCHEMA.md §Validation rules (plus the
/// field-range bounds documented in the schema tables). Collects ALL
/// violations into `errors`; returns true iff `c` is runnable. Pure
/// function of `c` — never mutates, never throws.
bool validateConfig(const Config& c, std::vector<std::string>& errors);

/// Canonical JSON text of ANY nlohmann DOM: object keys sorted
/// lexicographically (recursively), no whitespace, integers serialized as
/// integers (never drifted into float form), arrays keep their order.
/// Pure and total; the only inputs rejected by the JSON parser itself can
/// never reach this point.
std::string canonicalJson(const nlohmann::json& j);

/// Serialize a Config back to a DOM (keys land sorted — nlohmann objects
/// are ordered maps). Round-trip aid for tests/tools; the RESULT document
/// must still echo the ORIGINAL parsed config (see configHash()).
nlohmann::json configToJson(const Config& c);

/// canonicalJson(configToJson(c)) — canonical text of the round-tripped
/// model (NOT used for configHash; see below).
std::string canonicalConfigJson(const Config& c);

/// configHash = first 16 hex chars of sha256(canonicalJson(j)) where `j` is
/// the ORIGINAL parsed configuration DOM (NOT a re-serialization of Config)
/// so the hash is stable regardless of struct round-trip losses. Exactly
/// 16 lowercase hex characters.
std::string configHash(const nlohmann::json& originalParsed);

} // namespace micrort

#endif // MICRORT_CONFIG_HPP
