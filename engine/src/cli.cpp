// cli.cpp — micrort-engine subcommands: run / validate / schema / selftest.
//
// Stage 1 scope: `run` parses + validates and emits a stub result document
// ({"schema":"micrort-result/1","status":"stub",...}); agent 2-b2 replaces
// the stub with the full simulation result. Everything else is final.
//
// All output goes through std::cout / std::cerr (no printf) so test_cli can
// capture it in-process by swapping rdbuf. No exceptions escape runCli.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <micrort/cli.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <micrort/config.hpp>
#include <micrort/rng.hpp>
#include <micrort/scheduler.hpp>
#include <micrort/sha256.hpp>

namespace micrort {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------------
// Embedded fixtures used by `selftest` (kept in sync with the reference
// config of SIMULATION_SCHEMA.md; test_cli.cpp carries its own copies).
// ---------------------------------------------------------------------

const char* const kSelftestConfigA = R"JSON({
  "schema": "micrort-config/1",
  "name": "smoke",
  "duration": 50,
  "seed": 7,
  "contextSwitchCost": 1,
  "scheduler": {"type": "rr", "quantum": 4},
  "aging": {"interval": 20, "cap": 5},
  "memory": {"model": "region", "total": 512, "policy": "first_fit"},
  "resources": [
    {"id": "M1", "type": "mutex", "protocol": "inherit"},
    {"id": "S1", "type": "sem", "initial": 0, "max": 3},
    {"id": "Q1", "type": "msgq", "capacity": 4},
    {"id": "E1", "type": "evflags"}
  ],
  "tasks": [
    {"id": "P1", "kind": "periodic", "priority": 5, "arrival": 0, "period": 20,
     "relativeDeadline": 15, "steps": [
        {"op": "cpu", "d": 3}, {"op": "lock", "res": "M1"},
        {"op": "cpu", "d": 2}, {"op": "alloc", "size": 64, "tag": "buf"},
        {"op": "unlock", "res": "M1"}, {"op": "io", "d": 4, "dev": "disk"},
        {"op": "free", "tag": "buf"}]},
    {"id": "C1", "kind": "aperiodic", "priority": 3, "arrival": 2,
     "steps": [{"op": "recv", "res": "Q1"}, {"op": "cpu", "d": 2}]},
    {"id": "SP", "kind": "sporadic", "priority": 1, "arrival": 0,
     "releases": [3, 9, 21], "steps": [{"op": "cpu", "d": 1}]}
  ]
})JSON";

const char* const kSelftestConfigB = R"JSON({
  "schema": "micrort-config/1",
  "name": "selftest-b",
  "description": "pool memory + edf + every sync op",
  "seed": 123456789,
  "duration": 200,
  "contextSwitchCost": 2,
  "scheduler": {"type": "edf"},
  "aging": {"interval": 25, "cap": 7},
  "memory": {"model": "pool", "blockSize": 32, "blockCount": 8},
  "resources": [
    {"id": "M2", "type": "mutex", "protocol": "none"},
    {"id": "S2", "type": "sem", "initial": 2, "max": 5},
    {"id": "Q2", "type": "msgq", "capacity": 2},
    {"id": "E2", "type": "evflags"}
  ],
  "tasks": [
    {"id": "EDF_P", "kind": "periodic", "priority": 2, "arrival": 0, "period": 30,
     "relativeDeadline": 25, "jitter": 3, "steps": [
        {"op": "cpu", "d": 2}, {"op": "wait", "res": "S2"},
        {"op": "sleep", "d": 1}, {"op": "alloc", "size": 32},
        {"op": "signal", "res": "S2"},
        {"op": "evwait", "res": "E2", "mask": 5, "mode": "all"},
        {"op": "free", "tag": "a3"}, {"op": "cpu", "d": 1}]},
    {"id": "EDF_S", "kind": "sporadic", "priority": 1, "releases": [0, 40, 90],
     "steps": [{"op": "evset", "res": "E2", "mask": 5}, {"op": "cpu", "d": 3}]},
    {"id": "EDF_A", "kind": "aperiodic", "priority": 4, "arrival": 5, "steps": [
        {"op": "lock", "res": "M2"}, {"op": "send", "res": "Q2", "msg": 42},
        {"op": "cpu", "d": 2}, {"op": "unlock", "res": "M2"}]}
  ]
})JSON";

// ---------------------------------------------------------------------
// Output helpers (fixed shapes; manual assembly keeps the documented key
// order regardless of nlohmann's alphabetical object ordering).
// ---------------------------------------------------------------------

const char* const kUsage =
    "usage: micrort-engine <command> [options]\n"
    "commands:\n"
    "  run       --config <file|-> [--out <file|->] [--seed N]\n"
    "  validate  --config <file|->\n"
    "  schema\n"
    "  selftest\n";

std::string validationErrorJson(const std::vector<std::string>& details) {
    return "{\"error\":\"validation\",\"details\":" + json(details).dump() + "}";
}

std::string errorJson(const char* kind, const std::string& message) {
    json j;
    j["error"] = kind;
    j["message"] = message;
    return j.dump();
}

/// Read an entire stream (used for stdin and config files).
bool readAll(std::istream& in, std::string& out) {
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !in.bad();
}

bool readInput(const std::string& path, std::string& out, std::string& errMsg) {
    if (path == "-") {
        if (!readAll(std::cin, out)) {
            errMsg = "failed to read stdin";
            return false;
        }
        return true;
    }
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        errMsg = "cannot open file '" + path + "'";
        return false;
    }
    if (!readAll(f, out)) {
        errMsg = "failed to read file '" + path + "'";
        return false;
    }
    return true;
}

/// Parse "--seed N" values: full-string non-negative integer.
bool parseSeedArg(const std::string& s, std::int64_t& out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || v < 0) {
        return false;
    }
    out = static_cast<std::int64_t>(v);
    return true;
}

// ---------------------------------------------------------------------
// run
// ---------------------------------------------------------------------

int cmdRun(int argc, char** argv) {
    std::string configPath;
    std::string outPath = "-"; // stdout by default
    std::string seedStr;
    bool haveSeed = false;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            seedStr = argv[++i];
            haveSeed = true;
        } else if (a == "--config" || a == "--out" || a == "--seed") {
            std::cerr << errorJson("usage", a + " requires a value") << "\n";
            return 3;
        } else {
            std::cerr << errorJson("usage", "unknown option '" + a + "'") << "\n";
            return 3;
        }
    }
    if (configPath.empty()) {
        std::cerr << errorJson("usage", "--config is required") << "\n"
                  << kUsage;
        return 3;
    }

    std::int64_t seedOverride = 0;
    if (haveSeed && !parseSeedArg(seedStr, seedOverride)) {
        std::cerr << errorJson("usage", "--seed must be a non-negative integer")
                  << "\n";
        return 3;
    }

    std::string text;
    std::string ioErr;
    if (!readInput(configPath, text, ioErr)) {
        std::cerr << errorJson("io", ioErr) << "\n";
        return 3;
    }

    // Parse + validate. Malformed JSON is a validation error (exit 2).
    Config cfg;
    std::vector<std::string> errors;
    bool ok = true;
    json dom;
    try {
        dom = json::parse(text);
    } catch (const json::parse_error& e) {
        errors.push_back(std::string("json: malformed JSON: ") + e.what());
        ok = false;
    }
    if (ok) {
        ok = parseConfig(dom, cfg, errors) && validateConfig(cfg, errors);
    }
    if (!ok) {
        std::cerr << validationErrorJson(errors) << "\n";
        return 2;
    }

    if (haveSeed) {
        cfg.seed = static_cast<std::uint64_t>(seedOverride);
    }

    // Stage 1: stub result document (agent 2-b2 replaces this).
    const std::string result =
        "{\"schema\":\"micrort-result/1\",\"status\":\"stub\","
        "\"note\":\"engine core arrives in stage 2\"}";
    if (outPath == "-") {
        std::cout << result << "\n";
    } else {
        std::ofstream f(outPath, std::ios::out | std::ios::binary);
        if (!f.is_open()) {
            std::cerr << errorJson("io", "cannot write file '" + outPath + "'")
                      << "\n";
            return 3;
        }
        f << result << "\n";
    }
    std::cerr << "run: stub result for config '" << cfg.name << "' (validated: "
              << cfg.tasks.size() << " tasks, " << cfg.resources.size()
              << " resources)\n";
    return 0;
}

// ---------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------

int cmdValidate(int argc, char** argv) {
    std::string configPath;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (a == "--config") {
            std::cerr << errorJson("usage", "--config requires a value") << "\n";
            return 3;
        } else {
            std::cerr << errorJson("usage", "unknown option '" + a + "'") << "\n";
            return 3;
        }
    }
    if (configPath.empty()) {
        std::cerr << errorJson("usage", "--config is required") << "\n" << kUsage;
        return 3;
    }

    std::string text;
    std::string ioErr;
    if (!readInput(configPath, text, ioErr)) {
        std::cerr << errorJson("io", ioErr) << "\n";
        return 3;
    }

    json dom;
    try {
        dom = json::parse(text);
    } catch (const json::parse_error& e) {
        std::cout << "{\"valid\":false,\"details\":["
                  << json("json: malformed JSON: " + std::string(e.what())).dump()
                  << "]}\n";
        return 2;
    }

    Config cfg;
    std::vector<std::string> errors;
    if (!parseConfig(dom, cfg, errors) || !validateConfig(cfg, errors)) {
        std::cout << "{\"valid\":false,\"details\":" << json(errors).dump()
                  << "}\n";
        return 2;
    }
    std::cout << "{\"valid\":true,\"configHash\":\"" << configHash(dom)
              << "\"}\n";
    return 0;
}

// ---------------------------------------------------------------------
// schema
// ---------------------------------------------------------------------

int cmdSchema() {
    json j;
    j["config"] = "micrort-config/1";
    j["result"] = "micrort-result/1";
    j["schedulers"] = std::vector<std::string>{
        "fifo", "rr", "priority", "priority_p", "sjf", "srtf", "rm", "edf"};
    j["steps"] = std::vector<std::string>{
        "cpu", "io", "sleep", "lock", "unlock", "wait", "signal",
        "send", "recv", "evwait", "evset", "alloc", "free"};
    std::cout << j.dump() << "\n";
    return 0;
}

// ---------------------------------------------------------------------
// selftest
// ---------------------------------------------------------------------

int cmdSelftest() {
    int checks = 0;
    int failures = 0;
    const auto check = [&checks, &failures](bool ok, const std::string& what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "selftest FAIL: " << what << "\n";
        }
    };

    // --- sha256 ---
    check(sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256('abc') NIST vector");
    check(sha256_hex("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256('') NIST vector");
    {
        const std::string h = sha256_hex("micrort-lab");
        check(h.size() == 64, "sha256 digest length is 64");
        bool hexOnly = true;
        for (const char c : h) {
            const bool okChar = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            hexOnly = hexOnly && okChar;
        }
        check(hexOnly, "sha256 digest is lowercase hex");
        check(sha256_hex("micrort-lab") == h, "sha256 is deterministic");
    }
    {
        const std::string big(1000, 'x');
        const std::string small(999, 'x');
        check(sha256_hex(big) != sha256_hex(small), "sha256 multi-block input");
    }

    // --- rng ---
    {
        std::vector<std::uint64_t> a;
        std::vector<std::uint64_t> b;
        Xoshiro256 ra(42);
        Xoshiro256 rb(42);
        Xoshiro256 rc(43);
        for (int i = 0; i < 100; ++i) {
            a.push_back(ra.next());
            b.push_back(rb.next());
        }
        check(a == b, "rng: same seed -> same 100 values");
        bool anyDiff = false;
        for (int i = 0; i < 100; ++i) {
            anyDiff = anyDiff || (rc.next() != a[static_cast<std::size_t>(i)]);
        }
        check(anyDiff, "rng: different seed -> different stream");

        Xoshiro256 r1(0);
        bool boundedOk = true;
        for (int i = 0; i < 1000; ++i) {
            boundedOk = boundedOk && (r1.bounded(10) < 10);
        }
        Xoshiro256 r3(9);
        Xoshiro256 r4(9);
        bool boundedDet = true;
        for (int i = 0; i < 100; ++i) {
            boundedDet = boundedDet && (r3.bounded(1000) == r4.bounded(1000));
        }
        check(boundedOk, "rng: bounded(n) stays in [0,n)");
        check(boundedDet, "rng: bounded is deterministic");
        check(r1.bounded(1) == 0, "rng: bounded(1) == 0");
        check(r3.bounded(0) == 0, "rng: bounded(0) == 0 (documented)");
    }

    // --- canonical json ---
    {
        check(canonicalJson(json::parse(R"({"zz":1,"aa":2})")) ==
                  R"({"aa":2,"zz":1})",
              "canonical: keys sorted");
        check(canonicalJson(json::parse(R"({"b":{"y":1,"x":2},"a":[3,1,2]})")) ==
                  R"({"a":[3,1,2],"b":{"x":2,"y":1}})",
              "canonical: nested objects sorted, arrays preserved");
        check(canonicalJson(json::parse(R"({"n":5})")) == R"({"n":5})",
              "canonical: integers stay integers");
        check(canonicalJson(json::parse("  { \"a\" : 1 , \"b\": [ ] }")) ==
                  R"({"a":1,"b":[]})",
              "canonical: whitespace stripped");
        check(canonicalJson(json::parse(R"({"k":"a\"b\\c"})")) ==
                  R"({"k":"a\"b\\c"})",
              "canonical: string escaping preserved");
        const std::string t1 = R"({"x":1,"y":{"b":2,"a":3}})";
        const std::string t2 = json::parse(t1).dump(4); // pretty + reserialized
        check(canonicalJson(json::parse(t1)) == canonicalJson(json::parse(t2)),
              "canonical: independent of input formatting");
    }

    // --- config: two embedded samples ---
    {
        Config c;
        std::vector<std::string> errs;
        check(parseConfigText(kSelftestConfigA, c, errs), "config A parses");
        check(validateConfig(c, errs), "config A validates");
        check(configHash(json::parse(kSelftestConfigA)).size() == 16,
              "config A hash is 16 hex chars");
        check(c.scheduler.type == SchedType::RR && c.scheduler.quantum == 4,
              "config A: rr quantum default 4");
        check(c.memory.enabled && c.memory.model == MemModel::Region &&
                  c.memory.total == 512,
              "config A: region memory parsed");
        check(c.tasks.size() == 3 && c.resources.size() == 4,
              "config A: 3 tasks, 4 resources (declaration order)");
    }
    {
        Config c;
        std::vector<std::string> errs;
        check(parseConfigText(kSelftestConfigB, c, errs), "config B parses");
        check(validateConfig(c, errs), "config B validates");
        check(c.memory.enabled && c.memory.model == MemModel::Pool &&
                  c.memory.total == 256,
              "config B: pool total derived as blockSize*blockCount");
        check(c.tasks[0].steps[6].op == StepOp::FREE &&
                  c.tasks[0].steps[6].tag == "a3" &&
                  c.tasks[0].steps[3].op == StepOp::ALLOC &&
                  c.tasks[0].steps[3].tag == "a3",
              "config B: default alloc tag is a<stepIndex>");
        check(c.tasks[2].steps[1].op == StepOp::SEND &&
                  c.tasks[2].steps[1].msg == 42,
              "config B: send payload parsed");
    }
    {
        // Invalid config: unknown op must be reported, never crash.
        const std::string bad =
            R"({"schema":"micrort-config/1","name":"bad","duration":10,
                "scheduler":{"type":"fifo"},
                "tasks":[{"id":"A","kind":"aperiodic","steps":[{"op":"spin"}]}]})";
        Config c;
        std::vector<std::string> errs;
        const bool parsed = parseConfigText(bad, c, errs);
        check(!parsed, "bad config rejected");
        bool hasUnknownOp = false;
        for (const auto& e : errs) {
            hasUnknownOp = hasUnknownOp || e.find("unknown op") != std::string::npos;
        }
        check(hasUnknownOp, "bad config reports 'unknown op'");
    }
    {
        Config c;
        std::vector<std::string> errs;
        check(!parseConfigText("{\"name\":\"bad\"", c, errs),
              "malformed JSON rejected");
        check(!errs.empty() && errs[0].find("malformed JSON") != std::string::npos,
              "malformed JSON error message");
    }

    // --- scheduler ---
    {
        const std::vector<std::string> names = {
            "fifo", "rr", "priority", "priority_p", "sjf", "srtf", "rm", "edf"};
        for (const std::string& n : names) {
            SchedType t = SchedType::FIFO;
            check(schedTypeFromString(n, t) && schedTypeName(t) == n,
                  "scheduler name round-trip: " + n);
        }
        SchedType t = SchedType::FIFO;
        check(!schedTypeFromString("mlfq", t), "unknown scheduler name rejected");

        // Every type instantiates with the right identity.
        for (const std::string& n : names) {
            SchedType ty = SchedType::FIFO;
            (void)schedTypeFromString(n, ty);
            SchedulerCfg sc;
            sc.type = ty;
            if (ty == SchedType::RR) {
                sc.quantum = 4;
            }
            const auto s = makeScheduler(sc);
            check(s && s->type() == ty, "makeScheduler identity: " + n);
        }

        // RR quantum default (unvalidated cfg falls back to 4).
        SchedulerCfg rr;
        rr.type = SchedType::RR;
        rr.quantum = 0;
        check(makeScheduler(rr)->quantum() == 4, "rr default quantum is 4");

        // Ordering spot checks for the two sentinel-based schedulers.
        SchedCandidate a;
        SchedCandidate b;
        a.id = 1;
        b.id = 2;
        a.seq = 1;
        b.seq = 2;
        a.period = 10;
        b.period = kSchedAperiodicPeriod;
        check(makeScheduler(SchedulerCfg{SchedType::RM, 0})->compare(a, b) < 0,
              "rm: shorter period first, aperiodic last");
        a.period = 0; // raw 0 maps to the aperiodic sentinel
        b.period = 10;
        check(makeScheduler(SchedulerCfg{SchedType::RM, 0})->compare(a, b) > 0,
              "rm: period 0 treated as aperiodic");
        a.absoluteDeadline = 10;
        b.absoluteDeadline = MRT_TIME_MAX;
        a.period = 1;
        b.period = 1;
        check(makeScheduler(SchedulerCfg{SchedType::EDF, 0})->compare(a, b) < 0,
              "edf: earlier deadline first, none last");
    }

    const json summary = failures == 0
                             ? json{{"selftest", "ok"}, {"checks", checks}}
                             : json{{"selftest", "fail"},
                                    {"checks", checks},
                                    {"failed", failures}};
    std::cout << summary.dump() << "\n";
    return failures == 0 ? 0 : 3;
}

} // namespace

// ---------------------------------------------------------------------
// runCli
// ---------------------------------------------------------------------

int runCli(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << kUsage;
            return 3;
        }
        const std::string cmd = argv[1];
        if (cmd == "run") {
            return cmdRun(argc, argv);
        }
        if (cmd == "validate") {
            return cmdValidate(argc, argv);
        }
        if (cmd == "schema") {
            if (argc != 2) {
                std::cerr << errorJson("usage", "'schema' takes no options") << "\n";
                return 3;
            }
            return cmdSchema();
        }
        if (cmd == "selftest") {
            if (argc != 2) {
                std::cerr << errorJson("usage", "'selftest' takes no options") << "\n";
                return 3;
            }
            return cmdSelftest();
        }
        std::cerr << errorJson("usage", "unknown command '" + cmd + "'") << "\n"
                  << kUsage;
        return 3;
    } catch (const std::exception& e) {
        std::cerr << errorJson("internal", e.what()) << "\n";
        return 3;
    } catch (...) {
        std::cerr << errorJson("internal", "unknown exception") << "\n";
        return 3;
    }
}

} // namespace micrort
