// test_config.cpp — parse/validate coverage for micrort-config/1.
//
// Positive: the two fixtures (full field surface) + minimal config +
// canonical serialization / configHash stability.
// Negative: every schema validation rule (parse-level shape errors and
// validateConfig semantic errors) — each case asserts the overall verdict
// AND a recognizable fragment of the collected error text.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#define TH_SUITE "test_config"

#include "test_fixtures.hpp"
#include "test_harness.hpp"

#include <nlohmann/json.hpp>

#include <micrort/config.hpp>
#include <micrort/scheduler.hpp>
#include <micrort/sha256.hpp>

#include <string>
#include <vector>

using micrort::AgingCfg;
using micrort::Config;
using micrort::MemModel;
using micrort::MemoryCfg;
using micrort::ResCfg;
using micrort::ResKind;
using micrort::ResProtocol;
using micrort::SchedulerCfg;
using micrort::SchedType;
using micrort::StepCfg;
using micrort::StepOp;
using micrort::TaskCfg;
using micrort::TaskKind;
using nlohmann::json;

namespace {

bool errorsContain(const std::vector<std::string>& errs, const std::string& needle) {
    for (const auto& e : errs) {
        if (e.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dumpErrors(const std::vector<std::string>& errs) {
    for (const auto& e : errs) {
        std::printf("    err: %s\n", e.c_str());
    }
}

/// Minimal valid configuration used as the mutation base.
json baseConfig() {
    return json::parse(R"({
        "schema": "micrort-config/1",
        "name": "base",
        "duration": 10,
        "scheduler": {"type": "fifo"},
        "tasks": [{"id": "A", "kind": "aperiodic", "arrival": 0,
                   "steps": [{"op": "cpu", "d": 1}]}]
    })");
}

void expectValid(const std::string& text) {
    Config c;
    std::vector<std::string> errs;
    const bool parsed = micrort::parseConfigText(text, c, errs);
    const bool valid = parsed && micrort::validateConfig(c, errs);
    if (!valid) {
        std::printf("unexpectedly invalid:\n");
        dumpErrors(errs);
    }
    CHECK_TRUE(valid);
}

/// Full pipeline (parse + validate) must fail AND mention `needle`.
void expectInvalid(const std::string& text, const std::string& needle) {
    Config c;
    std::vector<std::string> errs;
    const bool parsed = micrort::parseConfigText(text, c, errs);
    const bool valid = parsed && micrort::validateConfig(c, errs);
    if (valid) {
        std::printf("unexpectedly valid (needle '%s')\n", needle.c_str());
    }
    CHECK_TRUE(!valid);
    CHECK_TRUE(errorsContain(errs, needle));
}

bool is16Hex(const std::string& s) {
    if (s.size() != 16) {
        return false;
    }
    for (const char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Positive: smoke fixture — every field surface asserted
    // ------------------------------------------------------------------
    TEST("smoke fixture parses+validates");
    {
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfigText(kFixtureSmoke, c, errs));
        CHECK_TRUE(micrort::validateConfig(c, errs));

        CHECK_STREQ(c.schema, "micrort-config/1");
        CHECK_STREQ(c.name, "smoke");
        CHECK_EQ(c.seed, 7u);
        CHECK_EQ(c.duration, 50);
        CHECK_EQ(c.cpus, 1);          // default
        CHECK_EQ(c.contextSwitchCost, 1);
        CHECK_TRUE(c.scheduler.type == SchedType::RR);
        CHECK_EQ(c.scheduler.quantum, 4);
        CHECK_TRUE(c.aging.enabled);
        CHECK_EQ(c.aging.interval, 20);
        CHECK_EQ(c.aging.cap, 5);
        CHECK_TRUE(c.memory.enabled);
        CHECK_TRUE(c.memory.model == MemModel::Region);
        CHECK_EQ(c.memory.total, 512);
        CHECK_TRUE(c.memory.policy == micrort::MemPolicy::First);
        CHECK_EQ(static_cast<int>(c.tasks.size()), 3);
        CHECK_EQ(static_cast<int>(c.resources.size()), 4);

        // Declaration order preserved (vector, never map).
        CHECK_STREQ(c.tasks[0].id, "P1");
        CHECK_STREQ(c.tasks[1].id, "C1");
        CHECK_STREQ(c.tasks[2].id, "SP");
        CHECK_STREQ(c.resources[0].id, "M1");
        CHECK_STREQ(c.resources[1].id, "S1");
        CHECK_STREQ(c.resources[2].id, "Q1");
        CHECK_STREQ(c.resources[3].id, "E1");

        // P1: periodic, all step kinds present in the program.
        const TaskCfg& p1 = c.tasks[0];
        CHECK_TRUE(p1.kind == TaskKind::Periodic);
        CHECK_EQ(p1.priority, 5);
        CHECK_EQ(p1.period, 20);
        CHECK_EQ(p1.relativeDeadline, 15);
        CHECK_EQ(p1.jitter, 0);
        CHECK_STREQ(p1.name, "P1"); // name defaults to id
        CHECK_EQ(static_cast<int>(p1.steps.size()), 7);
        CHECK_TRUE(p1.steps[0].op == StepOp::CPU && p1.steps[0].d == 3);
        CHECK_TRUE(p1.steps[1].op == StepOp::LOCK && p1.steps[1].res == "M1");
        CHECK_TRUE(p1.steps[2].op == StepOp::CPU && p1.steps[2].d == 2);
        CHECK_TRUE(p1.steps[3].op == StepOp::ALLOC && p1.steps[3].size == 64 &&
                   p1.steps[3].tag == "buf");
        CHECK_TRUE(p1.steps[4].op == StepOp::UNLOCK && p1.steps[4].res == "M1");
        CHECK_TRUE(p1.steps[5].op == StepOp::IO && p1.steps[5].d == 4 &&
                   p1.steps[5].dev == "disk");
        CHECK_TRUE(p1.steps[6].op == StepOp::FREE && p1.steps[6].tag == "buf");

        // C1: aperiodic with msgq recv.
        CHECK_TRUE(c.tasks[1].kind == TaskKind::Aperiodic);
        CHECK_EQ(c.tasks[1].arrival, 2);
        CHECK_TRUE(c.tasks[1].steps[0].op == StepOp::RECV &&
                   c.tasks[1].steps[0].res == "Q1");

        // SP: sporadic with sorted releases.
        CHECK_TRUE(c.tasks[2].kind == TaskKind::Sporadic);
        CHECK_EQ(static_cast<int>(c.tasks[2].releases.size()), 3);
        CHECK_EQ(c.tasks[2].releases[0], 3);
        CHECK_EQ(c.tasks[2].releases[2], 21);

        // Resources.
        CHECK_TRUE(c.resources[0].kind == ResKind::Mutex &&
                   c.resources[0].protocol == ResProtocol::Inherit);
        CHECK_TRUE(c.resources[1].kind == ResKind::Sem &&
                   c.resources[1].initial == 0 && c.resources[1].max == 3);
        CHECK_TRUE(c.resources[2].kind == ResKind::MsgQ &&
                   c.resources[2].capacity == 4);
        CHECK_TRUE(c.resources[3].kind == ResKind::EvFlags);
    }

    // ------------------------------------------------------------------
    // Positive: pool/EDF fixture — derived total, default alloc tag, msg
    // ------------------------------------------------------------------
    TEST("pool fixture parses+validates");
    {
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfigText(kFixturePool, c, errs));
        CHECK_TRUE(micrort::validateConfig(c, errs));
        CHECK_TRUE(c.scheduler.type == SchedType::EDF);
        CHECK_EQ(c.scheduler.quantum, 0); // non-rr -> no quantum
        CHECK_TRUE(c.memory.model == MemModel::Pool);
        CHECK_EQ(c.memory.blockSize, 32);
        CHECK_EQ(c.memory.blockCount, 8);
        CHECK_EQ(c.memory.total, 256); // derived blockSize * blockCount

        const TaskCfg& p = c.tasks[0];
        CHECK_EQ(p.jitter, 3);
        CHECK_TRUE(p.steps[3].op == StepOp::ALLOC && p.steps[3].tag == "a3");
        CHECK_TRUE(p.steps[5].op == StepOp::EVWAIT && p.steps[5].mask == 5 &&
                   p.steps[5].modeAll);
        CHECK_TRUE(p.steps[6].op == StepOp::FREE && p.steps[6].tag == "a3");
        CHECK_TRUE(p.steps[1].op == StepOp::WAIT && p.steps[1].res == "S2");
        CHECK_TRUE(p.steps[2].op == StepOp::SLEEP && p.steps[2].d == 1);
        CHECK_TRUE(c.tasks[1].steps[0].op == StepOp::EVSET &&
                   c.tasks[1].steps[0].mask == 5);
        CHECK_TRUE(c.tasks[2].steps[1].op == StepOp::SEND &&
                   c.tasks[2].steps[1].msg == 42);
        CHECK_EQ(static_cast<int>(c.tasks[1].releases.size()), 3);
    }

    // ------------------------------------------------------------------
    // Positive: minimal config + defaults + DOM-based parseConfig
    // ------------------------------------------------------------------
    TEST("minimal config defaults");
    expectValid(baseConfig().dump());
    {
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfigText(baseConfig().dump(), c, errs));
        CHECK_EQ(c.seed, 0u);
        CHECK_EQ(c.cpus, 1);
        CHECK_EQ(c.contextSwitchCost, 0);
        CHECK_TRUE(!c.aging.enabled);
        CHECK_TRUE(!c.memory.enabled);
        CHECK_TRUE(c.scheduler.type == SchedType::FIFO);
        CHECK_EQ(c.scheduler.quantum, 0);
        CHECK_EQ(static_cast<int>(c.resources.size()), 0);
        CHECK_STREQ(c.tasks[0].name, "A");
    }
    TEST("parseConfig accepts a DOM directly");
    {
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfig(json::parse(kFixtureSmoke), c, errs));
        CHECK_STREQ(c.name, "smoke");
    }
    TEST("rr without quantum defaults to 4");
    {
        json j = baseConfig();
        j["scheduler"] = json{{"type", "rr"}};
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfigText(j.dump(), c, errs));
        CHECK_TRUE(micrort::validateConfig(c, errs));
        CHECK_EQ(c.scheduler.quantum, 4);
    }

    // ------------------------------------------------------------------
    // configHash + canonicalJson stability
    // ------------------------------------------------------------------
    TEST("canonicalJson sorts keys recursively");
    {
        CHECK_STREQ(micrort::canonicalJson(json::parse(R"({"b":1,"a":2})")),
                    R"({"a":2,"b":1})");
        CHECK_STREQ(
            micrort::canonicalJson(json::parse(R"({"b":{"y":1,"x":2},"a":[3,1,2]})")),
            R"({"a":[3,1,2],"b":{"x":2,"y":1}})");
        CHECK_STREQ(micrort::canonicalJson(json::parse(R"({"n":5,"m":7})")),
                    R"({"m":7,"n":5})"); // integers stay integers
        CHECK_STREQ(micrort::canonicalJson(json::parse(" {\"a\" : 1} ")),
                    R"({"a":1})");
    }
    TEST("configHash stable across formatting, sensitive to content");
    {
        const json j1 = json::parse(kFixtureSmoke);
        const json j2 = json::parse(j1.dump(4)); // pretty reparse
        const std::string h1 = micrort::configHash(j1);
        const std::string h2 = micrort::configHash(j2);
        CHECK_TRUE(is16Hex(h1));
        CHECK_STREQ(h1, h2);

        json j3 = j1;
        j3["duration"] = 51;
        CHECK_TRUE(micrort::configHash(j3) != h1);

        // Same content, keys physically reordered in the source text:
        const std::string a = R"({"x":1,"y":2,"z":{"b":0,"a":0}})";
        const std::string b = R"({"z":{"a":0,"b":0},"y":2,"x":1})";
        CHECK_STREQ(micrort::canonicalJson(json::parse(a)),
                    micrort::canonicalJson(json::parse(b)));
    }
    TEST("config round-trip re-validates");
    {
        Config c;
        std::vector<std::string> errs;
        CHECK_TRUE(micrort::parseConfigText(kFixtureSmoke, c, errs));
        const std::string round = micrort::canonicalConfigJson(c);
        Config c2;
        CHECK_TRUE(micrort::parseConfigText(round, c2, errs));
        CHECK_TRUE(micrort::validateConfig(c2, errs));
        CHECK_EQ(static_cast<int>(c2.tasks.size()), 3);
        CHECK_STREQ(c2.tasks[0].id, "P1");
        CHECK_TRUE(c2.tasks[0].steps[3].tag == "buf");
    }

    // ------------------------------------------------------------------
    // Negative: parse-level problems (shape / types / enums)
    // ------------------------------------------------------------------
    TEST("malformed JSON is a collected error");
    expectInvalid("{\"name\":\"bad\"", "malformed JSON");

    TEST("unknown op string");
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0]["op"] = "spin";
        expectInvalid(j.dump(), "unknown op");
    }
    TEST("unknown scheduler type");
    {
        json j = baseConfig();
        j["scheduler"]["type"] = "mlfq";
        expectInvalid(j.dump(), "unknown scheduler type");
    }
    TEST("wrong field types");
    {
        json j = baseConfig();
        j["tasks"][0]["priority"] = "high";
        expectInvalid(j.dump(), "must be an integer");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0]["d"] = 1.5; // float where int required
        expectInvalid(j.dump(), "must be an integer");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"] = "nope";
        expectInvalid(j.dump(), "'steps'");
    }
    {
        json j = baseConfig();
        j["tasks"] = json{{"A", json::array()}}; // object, not array
        expectInvalid(j.dump(), "must be an array");
    }
    {
        json j = baseConfig();
        j["seed"] = -3;
        expectInvalid(j.dump(), "seed must be >= 0");
    }
    {
        json j = baseConfig();
        j["duration"] = "50";
        expectInvalid(j.dump(), "'duration'");
    }
    TEST("missing required fields");
    {
        json j = baseConfig();
        j.erase("name");
        expectInvalid(j.dump(), "missing required field 'name'");
    }
    {
        json j = baseConfig();
        j.erase("scheduler");
        expectInvalid(j.dump(), "missing required field 'scheduler'");
    }
    {
        json j = baseConfig();
        j.erase("tasks");
        expectInvalid(j.dump(), "missing required field 'tasks'");
    }
    {
        json j = baseConfig();
        j["tasks"][0].erase("id");
        expectInvalid(j.dump(), "missing required field 'id'");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0].erase("d");
        expectInvalid(j.dump(), "missing required field 'd'");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0] = json{{"op", "free"}, {"tag", "x"}};
        j["memory"] = json{{"model", "region"}, {"total", 128}};
        expectInvalid(j.dump(), "does not match any alloc tag");
    }
    TEST("unsupported schema version");
    {
        json j = baseConfig();
        j["schema"] = "micrort-config/2";
        expectInvalid(j.dump(), "unsupported schema");
    }

    // ------------------------------------------------------------------
    // Negative: validation rules (SIMULATION_SCHEMA.md)
    // ------------------------------------------------------------------
    TEST("duplicate task ids");
    {
        json j = baseConfig();
        j["tasks"].push_back(j["tasks"][0]); // same id "A"
        expectInvalid(j.dump(), "duplicate task id");
    }
    TEST("unknown resource reference");
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0] = json{{"op", "lock"}, {"res", "M9"}};
        expectInvalid(j.dump(), "unknown resource");
    }
    TEST("op / resource kind mismatch");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "S1"}, {"type", "sem"},
                                            {"initial", 0}, {"max", 3}}});
        j["tasks"][0]["steps"][0] = json{{"op", "lock"}, {"res", "S1"}};
        expectInvalid(j.dump(), "kind 'mutex'");
    }
    TEST("d out of range");
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0]["d"] = 0;
        expectInvalid(j.dump(), "d must be in 1..100000");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0]["d"] = 100001;
        expectInvalid(j.dump(), "d must be in 1..100000");
    }
    TEST("rr quantum bounds");
    {
        json j = baseConfig();
        j["scheduler"] = json{{"type", "rr"}, {"quantum", 0}};
        expectInvalid(j.dump(), "quantum must be in 1..1000");
    }
    {
        json j = baseConfig();
        j["scheduler"] = json{{"type", "rr"}, {"quantum", 1001}};
        expectInvalid(j.dump(), "quantum must be in 1..1000");
    }
    TEST("quantum on a non-rr scheduler");
    {
        json j = baseConfig();
        j["scheduler"] = json{{"type", "fifo"}, {"quantum", 4}};
        expectInvalid(j.dump(), "only applies to scheduler type 'rr'");
    }
    TEST("cpus must be 1");
    {
        json j = baseConfig();
        j["cpus"] = 2;
        expectInvalid(j.dump(), "only cpus == 1");
    }
    TEST("duration bounds");
    {
        json j = baseConfig();
        j["duration"] = 0;
        expectInvalid(j.dump(), "must be in 1..100000");
    }
    {
        json j = baseConfig();
        j["duration"] = -5;
        expectInvalid(j.dump(), "must be in 1..100000");
    }
    {
        json j = baseConfig();
        j["duration"] = 100001;
        expectInvalid(j.dump(), "must be in 1..100000");
    }
    TEST("contextSwitchCost bounds");
    {
        json j = baseConfig();
        j["contextSwitchCost"] = 11;
        expectInvalid(j.dump(), "must be in 0..10");
    }
    TEST("periodic without period");
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "periodic";
        expectInvalid(j.dump(), "'period'");
    }
    TEST("sporadic release problems");
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "sporadic";
        j["tasks"][0]["releases"] = json::array({9, 3, 21}); // unsorted
        expectInvalid(j.dump(), "strictly ascending");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "sporadic";
        j["tasks"][0]["releases"] = json::array(); // empty
        expectInvalid(j.dump(), "non-empty");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "sporadic";
        j["tasks"][0].erase("releases"); // missing
        expectInvalid(j.dump(), "'releases'");
    }
    TEST("jitter must be < period");
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "periodic";
        j["tasks"][0]["period"] = 5;
        j["tasks"][0]["jitter"] = 5;
        expectInvalid(j.dump(), "jitter must be in 0..period-1");
    }
    TEST("kind-specific fields on wrong kinds");
    {
        json j = baseConfig();
        j["tasks"][0]["period"] = 10; // aperiodic
        expectInvalid(j.dump(), "only valid for periodic");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "periodic";
        j["tasks"][0]["period"] = 10;
        j["tasks"][0]["releases"] = json::array({1, 2});
        expectInvalid(j.dump(), "only valid for sporadic");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "sporadic";
        j["tasks"][0]["releases"] = json::array({1, 2});
        j["tasks"][0]["jitter"] = 1;
        expectInvalid(j.dump(), "only valid for periodic");
    }
    TEST("free tag must match an alloc tag");
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 128}};
        j["tasks"][0]["steps"] = json::array(
            {json{{"op", "alloc"}, {"size", 8}, {"tag", "b1"}},
             json{{"op", "free"}, {"tag", "nope"}}});
        expectInvalid(j.dump(), "does not match any alloc tag");
    }
    TEST("free without tag is a parse error");
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 128}};
        j["tasks"][0]["steps"] = json::array(
            {json{{"op", "free"}}, json{{"op", "cpu"}, {"d", 1}}});
        expectInvalid(j.dump(), "missing required field 'tag'");
    }
    TEST("alloc/free require a memory block");
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0] = json{{"op", "alloc"}, {"size", 16}};
        expectInvalid(j.dump(), "require a 'memory'");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["steps"][0] = json{{"op", "free"}, {"tag", "a0"}};
        expectInvalid(j.dump(), "require a 'memory'");
    }
    TEST("default alloc tag is a<stepIndex> (free must use it)");
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 128}};
        j["tasks"][0]["steps"] = json::array(
            {json{{"op", "cpu"}, {"d", 1}},
             json{{"op", "alloc"}, {"size", 8}}, // default tag "a1"
             json{{"op", "free"}, {"tag", "a1"}}});
        expectValid(j.dump()); // matching default tag is fine
        j["tasks"][0]["steps"][2]["tag"] = "a0"; // wrong index
        expectInvalid(j.dump(), "does not match any alloc tag");
    }
    TEST("task id rules");
    {
        json j = baseConfig();
        j["tasks"][0]["id"] = "bad id!"; // charset + spaces
        expectInvalid(j.dump(), "is invalid");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["id"] = "abcdefghijklmnopqrstuvwxyzabcdef"; // 32 chars
        expectInvalid(j.dump(), "is invalid");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["id"] = ""; // empty
        expectInvalid(j.dump(), "is invalid");
    }
    TEST("priority range");
    {
        json j = baseConfig();
        j["tasks"][0]["priority"] = 101;
        expectInvalid(j.dump(), "priority must be in -100..100");
    }
    {
        json j = baseConfig();
        j["tasks"][0]["priority"] = -101;
        expectInvalid(j.dump(), "priority must be in -100..100");
    }
    TEST("negative arrival");
    {
        json j = baseConfig();
        j["tasks"][0]["arrival"] = -1;
        expectInvalid(j.dump(), "arrival must be >= 0");
    }
    TEST("negative relativeDeadline");
    {
        json j = baseConfig();
        j["tasks"][0]["relativeDeadline"] = -3;
        expectInvalid(j.dump(), "relativeDeadline must be >= 0");
    }
    TEST("steps must be non-empty");
    {
        json j = baseConfig();
        j["tasks"][0]["steps"] = json::array();
        expectInvalid(j.dump(), "at least 1 step");
    }
    TEST("step count cap (256)");
    {
        json j = baseConfig();
        json steps = json::array();
        for (int i = 0; i < 257; ++i) {
            steps.push_back(json{{"op", "cpu"}, {"d", 1}});
        }
        j["tasks"][0]["steps"] = steps;
        expectInvalid(j.dump(), "> 256");
    }
    TEST("task count cap (64)");
    {
        json j = baseConfig();
        for (int i = 1; i <= 64; ++i) { // 64 valid, 65th pushes over
            j["tasks"].push_back(json{{"id", "T" + std::to_string(i)},
                                      {"kind", "aperiodic"},
                                      {"steps", json::array({json{{"op", "cpu"},
                                                                  {"d", 1}}})}});
        }
        expectInvalid(j.dump(), "too many tasks");
    }
    TEST("resource count cap (32)");
    {
        json j = baseConfig();
        json res = json::array();
        for (int i = 0; i < 33; ++i) {
            res.push_back(json{{"id", "R" + std::to_string(i)}, {"type", "mutex"}});
        }
        j["resources"] = res;
        expectInvalid(j.dump(), "too many resources");
    }
    TEST("duplicate resource ids");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "M1"}, {"type", "mutex"}},
                                      json{{"id", "M1"}, {"type", "mutex"}}});
        j["tasks"][0]["steps"][0] = json{{"op", "lock"}, {"res", "M1"}};
        expectInvalid(j.dump(), "duplicate resource id");
    }
    TEST("sem bounds");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "S1"}, {"type", "sem"},
                                            {"initial", 0}, {"max", 0}}});
        j["tasks"][0]["steps"][0] = json{{"op", "wait"}, {"res", "S1"}};
        expectInvalid(j.dump(), "'max'");
    }
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "S1"}, {"type", "sem"},
                                            {"initial", 4}, {"max", 3}}});
        j["tasks"][0]["steps"][0] = json{{"op", "wait"}, {"res", "S1"}};
        expectInvalid(j.dump(), "'initial'");
    }
    TEST("msgq capacity must be >= 1");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "Q1"}, {"type", "msgq"},
                                            {"capacity", 0}}});
        j["tasks"][0]["steps"][0] = json{{"op", "recv"}, {"res", "Q1"}};
        expectInvalid(j.dump(), "'capacity'");
    }
    TEST("send requires msg");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "Q1"}, {"type", "msgq"},
                                            {"capacity", 2}}});
        j["tasks"][0]["steps"][0] = json{{"op", "send"}, {"res", "Q1"}};
        expectInvalid(j.dump(), "missing required field 'msg'");
    }
    TEST("msg must fit int32");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "Q1"}, {"type", "msgq"},
                                            {"capacity", 2}}});
        j["tasks"][0]["steps"][0] = json{{"op", "send"}, {"res", "Q1"},
                                          {"msg", 3000000000LL}};
        expectInvalid(j.dump(), "32-bit");
    }
    TEST("evwait/evset require mask");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "E1"}, {"type", "evflags"}}});
        j["tasks"][0]["steps"][0] = json{{"op", "evwait"}, {"res", "E1"}};
        expectInvalid(j.dump(), "missing required field 'mask'");
    }
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "E1"}, {"type", "evflags"}}});
        j["tasks"][0]["steps"][0] = json{{"op", "evset"}, {"res", "E1"},
                                          {"mask", -1}};
        expectInvalid(j.dump(), "mask must be in 0..4294967295");
    }
    TEST("evwait mode must be any|all");
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "E1"}, {"type", "evflags"}}});
        j["tasks"][0]["steps"][0] = json{{"op", "evwait"}, {"res", "E1"},
                                          {"mask", 3}, {"mode", "some"}};
        expectInvalid(j.dump(), "mode must be");
    }
    TEST("memory model rules");
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}}; // no total
        expectInvalid(j.dump(), "region model requires total");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "pool"}, {"policy", "first_fit"},
                           {"blockSize", 16}, {"blockCount", 4}};
        expectInvalid(j.dump(), "only applies to the region model");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 128},
                           {"blockSize", 16}};
        expectInvalid(j.dump(), "only applies to the pool model");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "pool"}, {"blockCount", 4}}; // no blockSize
        expectInvalid(j.dump(), "pool model requires blockSize");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "pool"}, {"blockSize", 16},
                           {"blockCount", 4}, {"total", 999}};
        expectInvalid(j.dump(), "pool total must equal");
    }
    TEST("alloc size must be >= 1");
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 128}};
        j["tasks"][0]["steps"][0] = json{{"op", "alloc"}, {"size", 0}};
        expectInvalid(j.dump(), "alloc size must be >= 1");
    }
    TEST("aging bounds");
    {
        json j = baseConfig();
        j["aging"] = json{{"interval", 0}, {"cap", 5}};
        expectInvalid(j.dump(), "interval must be >= 1");
    }
    {
        json j = baseConfig();
        j["aging"] = json{{"interval", 10}, {"cap", -1}};
        expectInvalid(j.dump(), "cap must be >= 0");
    }
    TEST("unknown enum strings");
    {
        json j = baseConfig();
        j["tasks"][0]["kind"] = "cyclic";
        expectInvalid(j.dump(), "unknown kind");
    }
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "X"}, {"type", "spinlock"}}});
        expectInvalid(j.dump(), "unknown resource type");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "slab"}, {"total", 64}};
        expectInvalid(j.dump(), "unknown memory model");
    }
    {
        json j = baseConfig();
        j["memory"] = json{{"model", "region"}, {"total", 64},
                           {"policy", "next_fit"}};
        expectInvalid(j.dump(), "unknown memory policy");
    }
    {
        json j = baseConfig();
        j["resources"] = json::array({json{{"id", "X"}, {"type", "mutex"},
                                            {"protocol", "ceiling"}}});
        expectInvalid(j.dump(), "unknown protocol");
    }
    TEST("multiple errors are all collected");
    {
        json j = baseConfig();
        j["duration"] = 0;                     // error 1
        j["cpus"] = 4;                         // error 2
        j["tasks"][0]["steps"][0]["d"] = 0;    // error 3
        Config c;
        std::vector<std::string> errs;
        const bool parsed = micrort::parseConfigText(j.dump(), c, errs);
        const bool valid = parsed && micrort::validateConfig(c, errs);
        CHECK_TRUE(!valid);
        CHECK_TRUE(errs.size() >= 3);
        CHECK_TRUE(errorsContain(errs, "duration: must be in 1..100000"));
        CHECK_TRUE(errorsContain(errs, "cpus: only cpus == 1"));
        CHECK_TRUE(errorsContain(errs, "d must be in 1..100000"));
    }

    return TEST_SUMMARY();
}
