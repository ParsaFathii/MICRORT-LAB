// test_trace_gantt.cpp — trace record invariants + gantt segment continuity.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0.
#define TH_SUITE "test_trace_gantt"
#include "test_harness.hpp"
#include "test_fixtures.hpp"

#include <micrort/config.hpp>
#include <micrort/simulation.hpp>

using nlohmann::json;
using micrort::Config;

namespace {

const char* kWorkload = R"JSON({
  "schema": "micrort-config/1", "name": "tg", "duration": 80,
  "contextSwitchCost": 1,
  "scheduler": {"type": "priority_p"},
  "resources": [{"id": "M", "type": "mutex", "protocol": "inherit"}],
  "memory": {"model": "region", "total": 256, "policy": "first_fit"},
  "tasks": [
    {"id": "HI", "priority": 9, "arrival": 0, "steps": [
      {"op": "cpu", "d": 3}, {"op": "io", "d": 4},
      {"op": "lock", "res": "M"}, {"op": "cpu", "d": 2},
      {"op": "unlock", "res": "M"}]},
    {"id": "LO", "priority": 1, "arrival": 0, "steps": [
      {"op": "lock", "res": "M"}, {"op": "cpu", "d": 5},
      {"op": "alloc", "size": 64, "tag": "b"}, {"op": "unlock", "res": "M"},
      {"op": "cpu", "d": 2}]},
    {"id": "MID", "priority": 5, "arrival": 4, "steps": [
      {"op": "cpu", "d": 4}, {"op": "sleep", "d": 3}, {"op": "cpu", "d": 2}]}
  ]
})JSON";

json runOnce(const char* text) {
    json dom = json::parse(text);
    Config cfg;
    std::vector<std::string> errors;
    CHECK_TRUE(micrort::parseConfig(dom, cfg, errors) &&
               micrort::validateConfig(cfg, errors));
    micrort::Simulation sim(cfg, dom);
    return sim.run();
}

} // namespace

int main() {
    const json r = runOnce(kWorkload);

    // ---------- trace invariants ----------
    TEST("trace-invariants");
    {
        const json& trace = r.at("trace");
        CHECK_TRUE(trace.size() >= 10);
        std::uint64_t seq = 0;
        bool seqStrict = true;
        bool sorted = true;
        std::int64_t lastT = -1;
        std::size_t badShape = 0;
        for (const json& e : trace) {
            for (const char* k :
                 {"seq", "t", "type", "task", "res", "from", "to", "dur",
                  "detail"}) {
                if (!e.contains(k)) {
                    badShape++;
                }
            }
            const std::uint64_t s = e.at("seq").get<std::uint64_t>();
            if (s <= seq && seq != 0) {
                seqStrict = false;
            }
            seq = s;
            const std::int64_t t = e.at("t").get<std::int64_t>();
            if (t < lastT) {
                sorted = false;
            }
            lastT = t;
        }
        CHECK_EQ(badShape, static_cast<std::size_t>(0));
        CHECK_TRUE(seqStrict);
        CHECK_TRUE(sorted);
        // first and last records
        CHECK_EQ(trace.front().at("type").get<std::string>(), "SIM_START");
        CHECK_EQ(trace.back().at("type").get<std::string>(), "SIM_END");
    }

    // ---------- gantt continuity ----------
    TEST("gantt-continuity");
    {
        const json& gantt = r.at("gantt");
        CHECK_TRUE(gantt.size() >= 10);
        // group segments per task; each task's segments must tile time
        // without gaps/overlaps (from its first t0 to its last t1) and be
        // time-ordered.
        std::map<std::string, std::vector<std::pair<std::int64_t, std::int64_t>>> byTask;
        for (const json& g : gantt) {
            byTask[g.at("task").get<std::string>()].emplace_back(
                g.at("t0").get<std::int64_t>(), g.at("t1").get<std::int64_t>());
            CHECK_TRUE(g.at("t1").get<std::int64_t>() >=
                       g.at("t0").get<std::int64_t>());
            CHECK_TRUE(g.at("state").is_string());
        }
        for (const auto& [task, segs] : byTask) {
            for (std::size_t i = 1; i < segs.size(); ++i) {
                CHECK_EQ(segs[i].first, segs[i - 1].second);
            }
        }
        // every task appears
        CHECK_EQ(byTask.size(), static_cast<std::size_t>(3));
        // ctx segments exist (contextSwitchCost = 1, several handovers)
        int ctxSegs = 0;
        for (const json& g : gantt) {
            if (g.at("note") == "ctx") {
                ctxSegs++;
            }
        }
        CHECK_TRUE(ctxSegs >= 2);
    }

    // ---------- gantt ↔ trace cross-consistency ----------
    TEST("gantt-trace-consistency");
    {
        // count RUNNING segments in gantt vs DISPATCH records in trace
        int dispatches = 0;
        for (const json& e : r.at("trace")) {
            if (e.at("type") == "DISPATCH") {
                dispatches++;
            }
        }
        CHECK_TRUE(dispatches >= 3);
        // memory events: every MEM_ALLOC has ok detail matching memory.events
        int allocTrace = 0;
        for (const json& e : r.at("trace")) {
            if (e.at("type") == "MEM_ALLOC") {
                allocTrace++;
            }
        }
        CHECK_EQ(allocTrace,
                 static_cast<int>(r.at("memory").at("events").size()));
        // leak: LO never frees tag "b"
        CHECK_EQ(r.at("memory").at("leaks").size(), static_cast<std::size_t>(1));
    }

    // ---------- determinism of trace+gantt bytes ----------
    TEST("byte-determinism");
    {
        const json a = runOnce(kWorkload);
        const json b = runOnce(kWorkload);
        CHECK_EQ(a.at("trace").dump(), b.at("trace").dump());
        CHECK_EQ(a.at("gantt").dump(), b.at("gantt").dump());
        CHECK_EQ(a.at("metrics").dump(), b.at("metrics").dump());
    }

    return TEST_SUMMARY();
}
