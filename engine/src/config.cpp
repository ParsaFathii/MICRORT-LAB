// config.cpp — experiment configuration parsing, validation and
// canonical serialization for micrort-config/1 (SIMULATION_SCHEMA.md).
//
// Design rules enforced here:
//   * NEVER throw: all problems are collected into the errors vector.
//   * Collect EVERY problem (parse continues after each error).
//   * Declaration order of tasks/resources is preserved (vectors).
//   * Field types are strict: a non-integer number where an integer is
//     expected is a parse error (no silent float truncation).
//   * null fields are treated as absent (result documents echo nulls).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <micrort/config.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include <micrort/scheduler.hpp> // schedTypeFromString
#include <micrort/sha256.hpp>    // configHash

namespace micrort {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------

/// Append "<path>: <msg>" (or just "<msg>" for the root) to the errors.
void fail(std::vector<std::string>& errs, const std::string& path,
          const std::string& msg) {
    errs.push_back(path.empty() ? msg : path + ": " + msg);
}

/// Human name of a JSON value's type (for error messages).
const char* jsonTypeName(const json& v) {
    switch (v.type()) {
    case json::value_t::null: return "null";
    case json::value_t::object: return "object";
    case json::value_t::array: return "array";
    case json::value_t::string: return "string";
    case json::value_t::boolean: return "boolean";
    case json::value_t::number_integer: return "integer";
    case json::value_t::number_unsigned: return "integer";
    case json::value_t::number_float: return "number";
    case json::value_t::binary: return "binary";
    case json::value_t::discarded: return "discarded";
    }
    return "unknown";
}

/// Field presence with the null-as-absent convention.
bool hasField(const json& obj, const char* key) {
    const auto it = obj.find(key);
    return it != obj.end() && !it->is_null();
}

/// Outcome of reading one field.
enum class Field { Absent, Ok, Bad };

/// Read an integer field. Absent leaves `out` untouched (caller preset a
/// default). Floats, strings, booleans, out-of-range unsigned values are
/// type errors. `obj` must be an object (callers check).
Field getInt(const json& obj, const char* key, std::int64_t& out,
             const std::string& path, std::vector<std::string>& errs) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return Field::Absent;
    }
    if (it->is_number_unsigned()) {
        const std::uint64_t u = it->get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(9223372036854775807LL)) {
            fail(errs, path, std::string("field '") + key +
                   "' is an integer too large for this engine");
            return Field::Bad;
        }
        out = static_cast<std::int64_t>(u);
        return Field::Ok;
    }
    if (it->is_number_integer()) {
        out = it->get<std::int64_t>();
        return Field::Ok;
    }
    fail(errs, path, std::string("field '") + key + "' must be an integer (got " +
         jsonTypeName(*it) + ")");
    return Field::Bad;
}

/// Read a string field. Same conventions as getInt.
Field getString(const json& obj, const char* key, std::string& out,
                const std::string& path, std::vector<std::string>& errs) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return Field::Absent;
    }
    if (it->is_string()) {
        out = it->get<std::string>();
        return Field::Ok;
    }
    fail(errs, path, std::string("field '") + key +
         "' must be a string (got " + jsonTypeName(*it) + ")");
    return Field::Bad;
}

/// Required integer field (missing field is an error).
bool reqInt(const json& obj, const char* key, std::int64_t& out,
            const std::string& path, std::vector<std::string>& errs) {
    const Field f = getInt(obj, key, out, path, errs);
    if (f == Field::Absent) {
        fail(errs, path, std::string("missing required field '") + key + "'");
        return false;
    }
    return f == Field::Ok;
}

/// Required string field (missing field is an error).
bool reqString(const json& obj, const char* key, std::string& out,
               const std::string& path, std::vector<std::string>& errs) {
    const Field f = getString(obj, key, out, path, errs);
    if (f == Field::Absent) {
        fail(errs, path, std::string("missing required field '") + key + "'");
        return false;
    }
    return f == Field::Ok;
}

/// Identifier rule shared by task and resource ids:
/// 1..31 chars of [A-Za-z0-9_-] (schema §Task object).
bool validIdChars(const std::string& id) {
    if (id.empty() || id.size() > 31) {
        return false;
    }
    for (const char ch : id) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// Read an integer ARRAY field into a vector (used for `releases`).
bool getIntArray(const json& obj, const char* key, std::vector<std::int64_t>& out,
                 const std::string& path, std::vector<std::string>& errs) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return false; // absent (caller decides whether that is an error)
    }
    if (!it->is_array()) {
        fail(errs, path, std::string("field '") + key + "' must be an array (got " +
             jsonTypeName(*it) + ")");
        return false;
    }
    out.clear();
    const std::string arrPath = path.empty() ? std::string(key)
                                             : path + "." + key;
    for (std::size_t i = 0; i < it->size(); ++i) {
        const json& e = (*it)[i];
        if (e.is_number_unsigned()) {
            const std::uint64_t u = e.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(9223372036854775807LL)) {
                fail(errs, arrPath + "[" + std::to_string(i) + "]",
                     "integer too large for this engine");
                continue;
            }
            out.push_back(static_cast<std::int64_t>(u));
        } else if (e.is_number_integer()) {
            out.push_back(e.get<std::int64_t>());
        } else {
            fail(errs, arrPath + "[" + std::to_string(i) + "]",
                 std::string("must be an integer (got ") + jsonTypeName(e) + ")");
        }
    }
    return true;
}

// ---------------------------------------------------------------------
// Step parsing
// ---------------------------------------------------------------------

bool parseStep(const json& j, StepCfg& out, std::size_t stepIndex,
               const std::string& path, std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("step must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }

    // op (required, closed enum)
    std::string opStr;
    if (reqString(j, "op", opStr, path, errs)) {
        if (!stepOpFromString(opStr, out.op)) {
            fail(errs, path, "unknown op '" + opStr +
                 "' (valid ops: cpu, io, sleep, lock, unlock, wait, signal, "
                 "send, recv, evwait, evset, alloc, free)");
        }
    }

    switch (out.op) {
    case StepOp::CPU:
    case StepOp::SLEEP:
        reqInt(j, "d", out.d, path, errs); // range checked in validateConfig
        break;
    case StepOp::IO:
        reqInt(j, "d", out.d, path, errs);
        getString(j, "dev", out.dev, path, errs); // optional label
        break;
    case StepOp::LOCK:
    case StepOp::UNLOCK:
    case StepOp::WAIT:
    case StepOp::SIGNAL:
    case StepOp::RECV:
        reqString(j, "res", out.res, path, errs);
        break;
    case StepOp::SEND:
        reqString(j, "res", out.res, path, errs);
        reqInt(j, "msg", out.msg, path, errs); // int32 range in validateConfig
        break;
    case StepOp::EVWAIT: {
        reqString(j, "res", out.res, path, errs);
        std::int64_t mask = 0;
        if (reqInt(j, "mask", mask, path, errs)) {
            if (mask < 0 || mask > 4294967295LL) {
                fail(errs, path, "mask must be in 0..4294967295");
            } else {
                out.mask = static_cast<std::uint32_t>(mask);
            }
        }
        std::string mode = "any"; // schema default
        if (getString(j, "mode", mode, path, errs) == Field::Ok) {
            if (mode == "any") {
                out.modeAll = false;
            } else if (mode == "all") {
                out.modeAll = true;
            } else {
                fail(errs, path, "mode must be \"any\" or \"all\" (got '" +
                     mode + "')");
            }
        }
        break;
    }
    case StepOp::EVSET: {
        reqString(j, "res", out.res, path, errs);
        std::int64_t mask = 0;
        if (reqInt(j, "mask", mask, path, errs)) {
            if (mask < 0 || mask > 4294967295LL) {
                fail(errs, path, "mask must be in 0..4294967295");
            } else {
                out.mask = static_cast<std::uint32_t>(mask);
            }
        }
        break;
    }
    case StepOp::ALLOC:
        reqInt(j, "size", out.size, path, errs); // >= 1 in validateConfig
        // Optional tag; default "a<stepIndex>" (0-based, schema §notes).
        out.tag = "a" + std::to_string(stepIndex);
        getString(j, "tag", out.tag, path, errs);
        break;
    case StepOp::FREE:
        // Tag is REQUIRED for free (schema step table).
        reqString(j, "tag", out.tag, path, errs);
        break;
    }
    return true;
}

// ---------------------------------------------------------------------
// Task / resource / memory / aging / scheduler parsing
// ---------------------------------------------------------------------

bool parseTask(const json& j, TaskCfg& out, const std::string& path,
               std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("task must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }

    reqString(j, "id", out.id, path, errs);
    out.name = out.id; // optional display name defaults to id
    getString(j, "name", out.name, path, errs);

    std::string kindStr = "aperiodic"; // kind is optional; default aperiodic
    if (getString(j, "kind", kindStr, path, errs) == Field::Ok) {
        if (!taskKindFromString(kindStr, out.kind)) {
            fail(errs, path, "unknown kind '" + kindStr +
                 "' (valid: aperiodic, periodic, sporadic)");
        }
    }

    {
        std::int64_t prio = 0;
        if (getInt(j, "priority", prio, path, errs) == Field::Ok) {
            if (prio < -2147483648LL || prio > 2147483647LL) {
                fail(errs, path, "priority does not fit a 32-bit integer");
            } else {
                out.priority = static_cast<std::int32_t>(prio);
            }
        }
    }
    getInt(j, "arrival", out.arrival, path, errs); // default 0
    getInt(j, "relativeDeadline", out.relativeDeadline, path, errs); // default 0

    // period: periodic only (required there), error on other kinds.
    if (out.kind == TaskKind::Periodic) {
        reqInt(j, "period", out.period, path, errs); // >= 1 in validateConfig
    } else if (hasField(j, "period")) {
        fail(errs, path, "field 'period' is only valid for periodic tasks");
    }

    // jitter: periodic only, optional.
    if (out.kind == TaskKind::Periodic) {
        getInt(j, "jitter", out.jitter, path, errs); // default 0
    } else if (hasField(j, "jitter")) {
        fail(errs, path, "field 'jitter' is only valid for periodic tasks");
    }

    // releases: sporadic only (required there, array of integers).
    if (out.kind == TaskKind::Sporadic) {
        if (!hasField(j, "releases")) {
            fail(errs, path, "missing required field 'releases'");
        } else {
            getIntArray(j, "releases", out.releases, path, errs);
        }
    } else if (hasField(j, "releases")) {
        fail(errs, path, "field 'releases' is only valid for sporadic tasks");
    }

    // steps: required, non-empty (non-emptiness in validateConfig).
    if (!hasField(j, "steps")) {
        fail(errs, path, "missing required field 'steps'");
    } else if (!j["steps"].is_array()) {
        fail(errs, path, std::string("field 'steps' must be an array (got ") +
             jsonTypeName(j["steps"]) + ")");
    } else {
        const json& steps = j["steps"];
        out.steps.reserve(steps.size());
        for (std::size_t k = 0; k < steps.size(); ++k) {
            StepCfg step;
            parseStep(steps[k], step, k, path + ".steps[" + std::to_string(k) + "]",
                      errs);
            out.steps.push_back(std::move(step));
        }
    }
    return true;
}

bool parseResource(const json& j, ResCfg& out, const std::string& path,
                   std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("resource must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }

    reqString(j, "id", out.id, path, errs);

    std::string typeStr = "mutex";
    if (reqString(j, "type", typeStr, path, errs)) {
        if (!resKindFromString(typeStr, out.kind)) {
            fail(errs, path, "unknown resource type '" + typeStr +
                 "' (valid: mutex, sem, msgq, evflags)");
        }
    }

    switch (out.kind) {
    case ResKind::Mutex: {
        std::string proto = "none";
        if (getString(j, "protocol", proto, path, errs) == Field::Ok) {
            if (!resProtocolFromString(proto, out.protocol)) {
                fail(errs, path, "unknown protocol '" + proto +
                     "' (valid: none, inherit)");
            }
        }
        break;
    }
    case ResKind::Sem:
        getInt(j, "initial", out.initial, path, errs); // default 0
        reqInt(j, "max", out.max, path, errs);         // >= 1 in validateConfig
        break;
    case ResKind::MsgQ:
        reqInt(j, "capacity", out.capacity, path, errs); // >= 1 in validateConfig
        break;
    case ResKind::EvFlags:
        break; // no fields
    }
    return true;
}

bool parseMemory(const json& j, MemoryCfg& out, const std::string& path,
                 std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("memory must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }
    out.enabled = true;

    std::string modelStr = "region";
    if (reqString(j, "model", modelStr, path, errs)) {
        if (!memModelFromString(modelStr, out.model)) {
            fail(errs, path, "unknown memory model '" + modelStr +
                 "' (valid: region, pool)");
        }
    }

    // policy: region only (default first_fit).
    std::string policyStr = "first_fit";
    const Field pf = getString(j, "policy", policyStr, path, errs);
    if (pf == Field::Ok && !memPolicyFromString(policyStr, out.policy)) {
        fail(errs, path, "unknown memory policy '" + policyStr +
             "' (valid: first_fit, best_fit, worst_fit)");
    }
    if (out.model == MemModel::Pool && hasField(j, "policy")) {
        fail(errs, path, "field 'policy' only applies to the region model");
    }

    getInt(j, "total", out.total, path, errs); // semantics depend on model

    // blockSize / blockCount: pool only.
    getInt(j, "blockSize", out.blockSize, path, errs);
    getInt(j, "blockCount", out.blockCount, path, errs);
    if (out.model == MemModel::Region) {
        if (hasField(j, "blockSize")) {
            fail(errs, path, "field 'blockSize' only applies to the pool model");
        }
        if (hasField(j, "blockCount")) {
            fail(errs, path, "field 'blockCount' only applies to the pool model");
        }
    }

    // Derive pool total when the author omitted it: blockSize * blockCount.
    if (out.model == MemModel::Pool && !hasField(j, "total") &&
        out.blockSize >= 1 && out.blockCount >= 1) {
        if (out.blockSize > 9223372036854775807LL / out.blockCount) {
            fail(errs, path, "blockSize * blockCount exceeds the int64 range");
        } else {
            out.total = out.blockSize * out.blockCount;
        }
    }
    return true;
}

bool parseAging(const json& j, AgingCfg& out, const std::string& path,
                std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("aging must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }
    out.enabled = true;
    reqInt(j, "interval", out.interval, path, errs); // >= 1 in validateConfig
    reqInt(j, "cap", out.cap, path, errs);           // >= 0 in validateConfig
    return true;
}

bool parseSchedulerBlock(const json& j, SchedulerCfg& out,
                          const std::string& path,
                          std::vector<std::string>& errs) {
    if (!j.is_object()) {
        fail(errs, path, std::string("scheduler must be an object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }

    std::string typeStr;
    if (reqString(j, "type", typeStr, path, errs)) {
        if (!schedTypeFromString(typeStr, out.type)) {
            fail(errs, path, "unknown scheduler type '" + typeStr +
                 "' (valid: fifo, rr, priority, priority_p, sjf, srtf, rm, edf)");
        }
    }

    // quantum: rr only; documented default 4 when rr omits it.
    out.quantum = (out.type == SchedType::RR) ? 4 : 0;
    getInt(j, "quantum", out.quantum, path, errs);
    if (out.type != SchedType::RR && hasField(j, "quantum")) {
        fail(errs, path, "field 'quantum' only applies to scheduler type 'rr'");
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------
// Public enum <-> string helpers
// ---------------------------------------------------------------------

const char* stepOpName(StepOp op) {
    switch (op) {
    case StepOp::CPU: return "cpu";
    case StepOp::IO: return "io";
    case StepOp::SLEEP: return "sleep";
    case StepOp::LOCK: return "lock";
    case StepOp::UNLOCK: return "unlock";
    case StepOp::WAIT: return "wait";
    case StepOp::SIGNAL: return "signal";
    case StepOp::SEND: return "send";
    case StepOp::RECV: return "recv";
    case StepOp::EVWAIT: return "evwait";
    case StepOp::EVSET: return "evset";
    case StepOp::ALLOC: return "alloc";
    case StepOp::FREE: return "free";
    }
    return "cpu"; // unreachable for valid enum values
}

bool stepOpFromString(const std::string& s, StepOp& out) {
    if (s == "cpu") { out = StepOp::CPU; return true; }
    if (s == "io") { out = StepOp::IO; return true; }
    if (s == "sleep") { out = StepOp::SLEEP; return true; }
    if (s == "lock") { out = StepOp::LOCK; return true; }
    if (s == "unlock") { out = StepOp::UNLOCK; return true; }
    if (s == "wait") { out = StepOp::WAIT; return true; }
    if (s == "signal") { out = StepOp::SIGNAL; return true; }
    if (s == "send") { out = StepOp::SEND; return true; }
    if (s == "recv") { out = StepOp::RECV; return true; }
    if (s == "evwait") { out = StepOp::EVWAIT; return true; }
    if (s == "evset") { out = StepOp::EVSET; return true; }
    if (s == "alloc") { out = StepOp::ALLOC; return true; }
    if (s == "free") { out = StepOp::FREE; return true; }
    return false;
}

const char* taskKindName(TaskKind k) {
    switch (k) {
    case TaskKind::Aperiodic: return "aperiodic";
    case TaskKind::Periodic: return "periodic";
    case TaskKind::Sporadic: return "sporadic";
    }
    return "aperiodic"; // unreachable
}

bool taskKindFromString(const std::string& s, TaskKind& out) {
    if (s == "aperiodic") { out = TaskKind::Aperiodic; return true; }
    if (s == "periodic") { out = TaskKind::Periodic; return true; }
    if (s == "sporadic") { out = TaskKind::Sporadic; return true; }
    return false;
}

const char* resKindName(ResKind k) {
    switch (k) {
    case ResKind::Mutex: return "mutex";
    case ResKind::Sem: return "sem";
    case ResKind::MsgQ: return "msgq";
    case ResKind::EvFlags: return "evflags";
    }
    return "mutex"; // unreachable
}

bool resKindFromString(const std::string& s, ResKind& out) {
    if (s == "mutex") { out = ResKind::Mutex; return true; }
    if (s == "sem") { out = ResKind::Sem; return true; }
    if (s == "msgq") { out = ResKind::MsgQ; return true; }
    if (s == "evflags") { out = ResKind::EvFlags; return true; }
    return false;
}

const char* resProtocolName(ResProtocol p) {
    switch (p) {
    case ResProtocol::None: return "none";
    case ResProtocol::Inherit: return "inherit";
    }
    return "none"; // unreachable
}

bool resProtocolFromString(const std::string& s, ResProtocol& out) {
    if (s == "none") { out = ResProtocol::None; return true; }
    if (s == "inherit") { out = ResProtocol::Inherit; return true; }
    return false;
}

const char* memModelName(MemModel m) {
    switch (m) {
    case MemModel::Region: return "region";
    case MemModel::Pool: return "pool";
    }
    return "region"; // unreachable
}

bool memModelFromString(const std::string& s, MemModel& out) {
    if (s == "region") { out = MemModel::Region; return true; }
    if (s == "pool") { out = MemModel::Pool; return true; }
    return false;
}

const char* memPolicyName(MemPolicy p) {
    switch (p) {
    case MemPolicy::First: return "first_fit";
    case MemPolicy::Best: return "best_fit";
    case MemPolicy::Worst: return "worst_fit";
    }
    return "first_fit"; // unreachable
}

bool memPolicyFromString(const std::string& s, MemPolicy& out) {
    if (s == "first_fit") { out = MemPolicy::First; return true; }
    if (s == "best_fit") { out = MemPolicy::Best; return true; }
    if (s == "worst_fit") { out = MemPolicy::Worst; return true; }
    return false;
}

// ---------------------------------------------------------------------
// parseConfig / parseConfigText
// ---------------------------------------------------------------------

bool parseConfig(const nlohmann::json& j, Config& out,
                 std::vector<std::string>& errors) {
    errors.clear();
    out = Config{}; // deterministic reset: callers only trust a true result

    if (!j.is_object()) {
        fail(errors, "", std::string("configuration root must be a JSON object (got ") +
             jsonTypeName(j) + ")");
        return false;
    }

    // schema (required, exact version string)
    if (reqString(j, "schema", out.schema, "", errors)) {
        if (out.schema != "micrort-config/1") {
            fail(errors, "schema", "unsupported schema '" + out.schema +
                 "' (expected 'micrort-config/1')");
        }
    }

    reqString(j, "name", out.name, "", errors); // non-empty in validateConfig
    getString(j, "description", out.description, "", errors); // optional

    // seed: optional, default 0; negative is rejected (it feeds a uint64 PRNG).
    {
        std::int64_t seed = 0;
        if (getInt(j, "seed", seed, "", errors) == Field::Ok) {
            if (seed < 0) {
                fail(errors, "seed", "seed must be >= 0");
            } else {
                out.seed = static_cast<std::uint64_t>(seed);
            }
        }
    }

    reqInt(j, "duration", out.duration, "", errors);     // 1..100000 in validate
    getInt(j, "cpus", out.cpus, "", errors);             // default 1, == 1 in validate
    getInt(j, "contextSwitchCost", out.contextSwitchCost, "", errors); // 0..10 in validate

    // scheduler (required)
    if (hasField(j, "scheduler")) {
        parseSchedulerBlock(j["scheduler"], out.scheduler, "scheduler", errors);
    } else {
        fail(errors, "", "missing required field 'scheduler'");
    }

    // aging (optional)
    if (hasField(j, "aging")) {
        parseAging(j["aging"], out.aging, "aging", errors);
    }

    // memory (optional; absent => alloc/free forbidden)
    if (hasField(j, "memory")) {
        parseMemory(j["memory"], out.memory, "memory", errors);
    }

    // resources (optional, declaration order preserved)
    if (hasField(j, "resources")) {
        if (j["resources"].is_array()) {
            out.resources.reserve(j["resources"].size());
            for (std::size_t i = 0; i < j["resources"].size(); ++i) {
                ResCfg r;
                parseResource(j["resources"][i], r,
                              "resources[" + std::to_string(i) + "]", errors);
                out.resources.push_back(std::move(r));
            }
        } else {
            fail(errors, "resources",
                 std::string("must be an array (got ") + jsonTypeName(j["resources"]) + ")");
        }
    }

    // tasks (required, declaration order preserved)
    if (hasField(j, "tasks")) {
        if (j["tasks"].is_array()) {
            out.tasks.reserve(j["tasks"].size());
            for (std::size_t i = 0; i < j["tasks"].size(); ++i) {
                TaskCfg t;
                parseTask(j["tasks"][i], t, "tasks[" + std::to_string(i) + "]",
                          errors);
                out.tasks.push_back(std::move(t));
            }
        } else {
            fail(errors, "tasks",
                 std::string("must be an array (got ") + jsonTypeName(j["tasks"]) + ")");
        }
    } else {
        fail(errors, "", "missing required field 'tasks'");
    }

    return errors.empty();
}

bool parseConfigText(const std::string& text, Config& out,
                     std::vector<std::string>& errors) {
    errors.clear();
    out = Config{};
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        errors.push_back(std::string("json: malformed JSON: ") + e.what());
        return false;
    } catch (const std::exception& e) {
        errors.push_back(std::string("json: parse failed: ") + e.what());
        return false;
    } catch (...) {
        errors.push_back("json: parse failed: unknown error");
        return false;
    }
    return parseConfig(parsed, out, errors);
}

// ---------------------------------------------------------------------
// validateConfig — every rule of SIMULATION_SCHEMA.md §Validation rules
// ---------------------------------------------------------------------

bool validateConfig(const Config& c, std::vector<std::string>& errors) {
    errors.clear();

    // --- global scalars ---
    if (c.duration < 1 || c.duration > 100000) {
        fail(errors, "duration", "must be in 1..100000 (got " +
             std::to_string(c.duration) + ")");
    }
    if (c.cpus != 1) {
        fail(errors, "cpus", "only cpus == 1 is supported in v1 (got " +
             std::to_string(c.cpus) + ")");
    }
    if (c.contextSwitchCost < 0 || c.contextSwitchCost > 10) {
        fail(errors, "contextSwitchCost", "must be in 0..10 (got " +
             std::to_string(c.contextSwitchCost) + ")");
    }
    if (c.scheduler.type == SchedType::RR) {
        if (c.scheduler.quantum < 1 || c.scheduler.quantum > 1000) {
            fail(errors, "scheduler", "rr quantum must be in 1..1000 (got " +
                 std::to_string(c.scheduler.quantum) + ")");
        }
    } else if (c.scheduler.quantum != 0) {
        // Parse already rejects this; kept for direct callers of the struct.
        fail(errors, "scheduler", "quantum only applies to scheduler type 'rr'");
    }
    if (c.tasks.size() > 64) {
        fail(errors, "tasks", "too many tasks (" + std::to_string(c.tasks.size()) +
             " > 64)");
    }
    if (c.resources.size() > 32) {
        fail(errors, "resources",
             "too many resources (" + std::to_string(c.resources.size()) + " > 32)");
    }

    // --- resources: ids, per-kind bounds ---
    std::map<std::string, ResKind> resById;
    for (std::size_t i = 0; i < c.resources.size(); ++i) {
        const ResCfg& r = c.resources[i];
        const std::string path = "resources[" + std::to_string(i) + "]";
        if (!validIdChars(r.id)) {
            fail(errors, path, "id '" + r.id +
                 "' is invalid: must be 1..31 chars of [A-Za-z0-9_-]");
        }
        if (resById.count(r.id) != 0) {
            fail(errors, path, "duplicate resource id '" + r.id + "'");
        } else {
            resById[r.id] = r.kind;
        }
        if (r.kind == ResKind::Sem) {
            if (r.max < 1) {
                fail(errors, path, "sem 'max' must be >= 1 (got " +
                     std::to_string(r.max) + ")");
            }
            if (r.initial < 0 || (r.max >= 1 && r.initial > r.max)) {
                fail(errors, path, "sem 'initial' must be in 0..max (got " +
                     std::to_string(r.initial) + ")");
            }
        } else if (r.kind == ResKind::MsgQ) {
            if (r.capacity < 1) {
                fail(errors, path, "msgq 'capacity' must be >= 1 (got " +
                     std::to_string(r.capacity) + ")");
            }
        }
    }

    // --- memory block ---
    if (c.memory.enabled) {
        if (c.memory.model == MemModel::Region) {
            if (c.memory.total < 1) {
                fail(errors, "memory",
                     "region model requires total >= 1 (got " +
                     std::to_string(c.memory.total) + ")");
            }
        } else { // pool
            if (c.memory.blockSize < 1) {
                fail(errors, "memory", "pool model requires blockSize >= 1 (got " +
                     std::to_string(c.memory.blockSize) + ")");
            }
            if (c.memory.blockCount < 1) {
                fail(errors, "memory", "pool model requires blockCount >= 1 (got " +
                     std::to_string(c.memory.blockCount) + ")");
            }
            if (c.memory.blockSize >= 1 && c.memory.blockCount >= 1 &&
                c.memory.blockSize <= 9223372036854775807LL / c.memory.blockCount) {
                const std::int64_t product =
                    c.memory.blockSize * c.memory.blockCount;
                if (c.memory.total != product) {
                    fail(errors, "memory",
                         "pool total must equal blockSize * blockCount (" +
                         std::to_string(product) + ", got " +
                         std::to_string(c.memory.total) + ")");
                }
            }
        }
    }

    // --- aging block ---
    if (c.aging.enabled) {
        if (c.aging.interval < 1) {
            fail(errors, "aging", "interval must be >= 1 (got " +
                 std::to_string(c.aging.interval) + ")");
        }
        if (c.aging.cap < 0) {
            fail(errors, "aging", "cap must be >= 0 (got " +
                 std::to_string(c.aging.cap) + ")");
        }
    }

    // --- tasks ---
    std::set<std::string> taskIds;
    for (std::size_t i = 0; i < c.tasks.size(); ++i) {
        const TaskCfg& t = c.tasks[i];
        const std::string path = "tasks[" + std::to_string(i) + "]";

        if (!validIdChars(t.id)) {
            fail(errors, path, "id '" + t.id +
                 "' is invalid: must be 1..31 chars of [A-Za-z0-9_-]");
        }
        if (taskIds.count(t.id) != 0) {
            fail(errors, path, "duplicate task id '" + t.id + "'");
        } else {
            taskIds.insert(t.id);
        }

        if (t.priority < -100 || t.priority > 100) {
            fail(errors, path, "priority must be in -100..100 (got " +
                 std::to_string(t.priority) + ")");
        }
        if (t.arrival < 0) {
            fail(errors, path, "arrival must be >= 0 (got " +
                 std::to_string(t.arrival) + ")");
        }
        if (t.relativeDeadline < 0) {
            fail(errors, path, "relativeDeadline must be >= 0 (got " +
                 std::to_string(t.relativeDeadline) + ")");
        }

        if (t.kind == TaskKind::Periodic) {
            if (t.period < 1) {
                fail(errors, path, "periodic task requires period >= 1 (got " +
                     std::to_string(t.period) + ")");
            }
            if (t.period >= 1 && (t.jitter < 0 || t.jitter >= t.period)) {
                fail(errors, path, "jitter must be in 0..period-1 (got " +
                     std::to_string(t.jitter) + ", period " +
                     std::to_string(t.period) + ")");
            }
        } else if (t.kind == TaskKind::Sporadic) {
            if (t.releases.empty()) {
                fail(errors, path,
                     "sporadic task requires a non-empty 'releases' array");
            } else {
                for (std::size_t k = 0; k < t.releases.size(); ++k) {
                    if (t.releases[k] < 0) {
                        fail(errors, path + ".releases",
                             "release ticks must be >= 0 (got " +
                             std::to_string(t.releases[k]) + ")");
                    }
                    if (k > 0 && t.releases[k] <= t.releases[k - 1]) {
                        fail(errors, path + ".releases",
                             "release ticks must be strictly ascending");
                    }
                }
            }
        }

        if (t.steps.empty()) {
            fail(errors, path + ".steps", "must contain at least 1 step");
        }
        if (t.steps.size() > 256) {
            fail(errors, path + ".steps",
                 "too many steps (" + std::to_string(t.steps.size()) + " > 256)");
        }

        // Pass 1: every alloc tag of this task (explicit or default
        // "a<stepIndex>" — the parser resolved defaults already).
        std::set<std::string> allocTags;
        for (const StepCfg& s : t.steps) {
            if (s.op == StepOp::ALLOC) {
                allocTags.insert(s.tag);
            }
        }

        // Pass 2: per-step rules.
        for (std::size_t k = 0; k < t.steps.size(); ++k) {
            const StepCfg& s = t.steps[k];
            const std::string spath = path + ".steps[" + std::to_string(k) + "]";

            switch (s.op) {
            case StepOp::CPU:
            case StepOp::IO:
            case StepOp::SLEEP:
                if (s.d < 1 || s.d > 100000) {
                    fail(errors, spath, "d must be in 1..100000 (got " +
                         std::to_string(s.d) + ")");
                }
                break;
            case StepOp::ALLOC:
                if (s.size < 1) {
                    fail(errors, spath, "alloc size must be >= 1 (got " +
                         std::to_string(s.size) + ")");
                }
                if (!c.memory.enabled) {
                    fail(errors, spath,
                         "alloc steps require a 'memory' block in the configuration");
                }
                break;
            case StepOp::FREE:
                if (!c.memory.enabled) {
                    fail(errors, spath,
                         "free steps require a 'memory' block in the configuration");
                }
                if (allocTags.count(s.tag) == 0) {
                    fail(errors, spath, "free tag '" + s.tag +
                         "' does not match any alloc tag in this task");
                }
                break;
            case StepOp::SEND:
                if (s.msg < -2147483648LL || s.msg > 2147483647LL) {
                    fail(errors, spath,
                         "msg must fit in a 32-bit integer (got " +
                         std::to_string(s.msg) + ")");
                }
                break;
            default:
                break; // resource ops handled below
            }

            // Resource reference checks (existence + op/kind match).
            const bool usesRes =
                s.op == StepOp::LOCK || s.op == StepOp::UNLOCK ||
                s.op == StepOp::WAIT || s.op == StepOp::SIGNAL ||
                s.op == StepOp::SEND || s.op == StepOp::RECV ||
                s.op == StepOp::EVWAIT || s.op == StepOp::EVSET;
            if (usesRes) {
                const auto it = resById.find(s.res);
                if (it == resById.end()) {
                    fail(errors, spath, "unknown resource '" + s.res + "'");
                } else {
                    ResKind want = ResKind::Mutex;
                    switch (s.op) {
                    case StepOp::LOCK:
                    case StepOp::UNLOCK:
                        want = ResKind::Mutex;
                        break;
                    case StepOp::WAIT:
                    case StepOp::SIGNAL:
                        want = ResKind::Sem;
                        break;
                    case StepOp::SEND:
                    case StepOp::RECV:
                        want = ResKind::MsgQ;
                        break;
                    case StepOp::EVWAIT:
                    case StepOp::EVSET:
                        want = ResKind::EvFlags;
                        break;
                    default:
                        break;
                    }
                    if (it->second != want) {
                        fail(errors, spath, std::string("op '") +
                             stepOpName(s.op) +
                             "' requires a resource of kind '" + resKindName(want) +
                             "' but '" + s.res + "' is a '" +
                             resKindName(it->second) + "'");
                    }
                }
            }
        }
    }

    return errors.empty();
}

// ---------------------------------------------------------------------
// Canonical serialization
// ---------------------------------------------------------------------

namespace {

/// Recursive canonical writer: objects emit keys in lexicographic order,
/// arrays keep element order, scalars are emitted by nlohmann's scalar
/// dump (integers stay integers, strings stay escaped, floats round-trip).
void canonicalAppend(const json& j, std::string& out) {
    switch (j.type()) {
    case json::value_t::object: {
        out.push_back('{');
        std::vector<std::string> keys;
        keys.reserve(j.size());
        for (auto it = j.begin(); it != j.end(); ++it) {
            keys.push_back(it.key());
        }
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const std::string& k : keys) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            out += json(k).dump(); // key scalar, escaped
            out.push_back(':');
            canonicalAppend(j.at(k), out);
        }
        out.push_back('}');
        break;
    }
    case json::value_t::array: {
        out.push_back('[');
        bool first = true;
        for (const auto& e : j) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            canonicalAppend(e, out);
        }
        out.push_back(']');
        break;
    }
    default:
        out += j.dump(); // null / bool / string / number
        break;
    }
}

} // namespace

std::string canonicalJson(const nlohmann::json& j) {
    std::string out;
    out.reserve(256);
    canonicalAppend(j, out);
    return out;
}

nlohmann::json configToJson(const Config& c) {
    json j;
    j["schema"] = c.schema;
    j["name"] = c.name;
    if (!c.description.empty()) {
        j["description"] = c.description;
    }
    j["seed"] = c.seed;
    j["duration"] = c.duration;
    j["cpus"] = c.cpus;
    j["contextSwitchCost"] = c.contextSwitchCost;

    {
        json s;
        s["type"] = schedTypeName(c.scheduler.type);
        if (c.scheduler.type == SchedType::RR) {
            s["quantum"] = c.scheduler.quantum;
        }
        j["scheduler"] = std::move(s);
    }
    if (c.aging.enabled) {
        json a;
        a["interval"] = c.aging.interval;
        a["cap"] = c.aging.cap;
        j["aging"] = std::move(a);
    }
    if (c.memory.enabled) {
        json m;
        m["model"] = memModelName(c.memory.model);
        if (c.memory.model == MemModel::Region) {
            m["total"] = c.memory.total;
            m["policy"] = memPolicyName(c.memory.policy);
        } else {
            m["blockSize"] = c.memory.blockSize;
            m["blockCount"] = c.memory.blockCount;
            m["total"] = c.memory.total;
        }
        j["memory"] = std::move(m);
    }

    json resources = json::array();
    for (const ResCfg& r : c.resources) {
        json e;
        e["id"] = r.id;
        e["type"] = resKindName(r.kind);
        switch (r.kind) {
        case ResKind::Mutex:
            e["protocol"] = resProtocolName(r.protocol);
            break;
        case ResKind::Sem:
            e["initial"] = r.initial;
            e["max"] = r.max;
            break;
        case ResKind::MsgQ:
            e["capacity"] = r.capacity;
            break;
        case ResKind::EvFlags:
            break;
        }
        resources.push_back(std::move(e));
    }
    j["resources"] = std::move(resources);

    json tasks = json::array();
    for (const TaskCfg& t : c.tasks) {
        json e;
        e["id"] = t.id;
        if (t.name != t.id) {
            e["name"] = t.name;
        }
        e["kind"] = taskKindName(t.kind);
        e["priority"] = t.priority;
        e["arrival"] = t.arrival;
        if (t.kind == TaskKind::Periodic) {
            e["period"] = t.period;
            if (t.jitter != 0) {
                e["jitter"] = t.jitter;
            }
        }
        if (t.kind == TaskKind::Sporadic) {
            e["releases"] = t.releases;
        }
        if (t.relativeDeadline != 0) {
            e["relativeDeadline"] = t.relativeDeadline;
        }
        json steps = json::array();
        for (const StepCfg& s : t.steps) {
            json sp;
            sp["op"] = stepOpName(s.op);
            switch (s.op) {
            case StepOp::CPU:
            case StepOp::SLEEP:
                sp["d"] = s.d;
                break;
            case StepOp::IO:
                sp["d"] = s.d;
                if (!s.dev.empty()) {
                    sp["dev"] = s.dev;
                }
                break;
            case StepOp::LOCK:
            case StepOp::UNLOCK:
            case StepOp::WAIT:
            case StepOp::SIGNAL:
            case StepOp::RECV:
                sp["res"] = s.res;
                break;
            case StepOp::SEND:
                sp["res"] = s.res;
                sp["msg"] = s.msg;
                break;
            case StepOp::EVWAIT:
                sp["res"] = s.res;
                sp["mask"] = s.mask;
                sp["mode"] = s.modeAll ? "all" : "any";
                break;
            case StepOp::EVSET:
                sp["res"] = s.res;
                sp["mask"] = s.mask;
                break;
            case StepOp::ALLOC:
                sp["size"] = s.size;
                sp["tag"] = s.tag;
                break;
            case StepOp::FREE:
                sp["tag"] = s.tag;
                break;
            }
            steps.push_back(std::move(sp));
        }
        e["steps"] = std::move(steps);
        tasks.push_back(std::move(e));
    }
    j["tasks"] = std::move(tasks);
    return j;
}

std::string canonicalConfigJson(const Config& c) {
    return canonicalJson(configToJson(c));
}

std::string configHash(const nlohmann::json& originalParsed) {
    return sha256_hex(canonicalJson(originalParsed)).substr(0, 16);
}

} // namespace micrort
