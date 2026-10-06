// test_simulation.cpp — event loop, program interpretation, schedulers,
// preemption, quantum, context-switch cost, sync ops, deadlines, edge cases.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0.
#define TH_SUITE "test_simulation"
#include "test_harness.hpp"
#include "test_fixtures.hpp"

#include <micrort/config.hpp>
#include <micrort/simulation.hpp>

using nlohmann::json;
using micrort::Config;

namespace {

micrort::Simulation simOf(const std::string& text) {
    Config cfg;
    std::vector<std::string> errors;
    json dom = json::parse(text);
    const bool ok = micrort::parseConfig(dom, cfg, errors) &&
                    micrort::validateConfig(cfg, errors);
    CHECK_TRUE(ok);
    if (!ok) {
        for (const std::string& e : errors) {
            std::printf("  parse/validate: %s\n", e.c_str());
        }
    }
    return micrort::Simulation(cfg, dom);
}

int countEvents(const json& r, const std::string& type) {
    int n = 0;
    for (const json& e : r.at("trace")) {
        if (e.at("type") == type) {
            n++;
        }
    }
    return n;
}

bool hasEventWhere(const json& r, const std::string& type,
                   const std::function<bool(const json&)>& pred) {
    for (const json& e : r.at("trace")) {
        if (e.at("type") == type && pred(e)) {
            return true;
        }
    }
    return false;
}

std::string dispatchOrder(const json& r) {
    std::string s;
    for (const json& e : r.at("trace")) {
        if (e.at("type") == "DISPATCH") {
            if (!s.empty()) {
                s += ",";
            }
            s += e.at("task").get<std::string>();
        }
    }
    return s;
}

// Workload: 4 aperiodic tasks, no sync — clean scheduling semantics.
const char* kFourTasks = R"JSON({
  "schema": "micrort-config/1", "name": "four", "duration": 60,
  "scheduler": {"type": "fifo"},
  "tasks": [
    {"id": "A", "priority": 1, "arrival": 0, "steps": [{"op": "cpu", "d": 5}]},
    {"id": "B", "priority": 5, "arrival": 0, "steps": [{"op": "cpu", "d": 3}]},
    {"id": "C", "priority": 3, "arrival": 2, "steps": [{"op": "cpu", "d": 2}]},
    {"id": "D", "priority": 9, "arrival": 9, "steps": [{"op": "cpu", "d": 4}]}
  ]
})JSON";

} // namespace

int main() {
    // ---------- every scheduler completes the 4-task workload ----------
    TEST("all-schedulers-complete");
    {
        const char* types[] = {"fifo", "rr", "priority", "priority_p",
                               "sjf", "srtf", "rm", "edf"};
        for (const char* ty : types) {
            json dom = json::parse(kFourTasks);
            dom["scheduler"] = json{{"type", ty}};
            if (std::string(ty) == "rr") {
                dom["scheduler"]["quantum"] = 3;
            }
            Config cfg;
            std::vector<std::string> errors;
            CHECK_TRUE(micrort::parseConfig(dom, cfg, errors) &&
                       micrort::validateConfig(cfg, errors));
            micrort::Simulation sim(cfg, dom);
            const json r = sim.run();
            CHECK_EQ(r.at("status").get<std::string>(), "completed");
            CHECK_EQ(r.at("metrics").at("completedJobs").get<int>(), 4);
            CHECK_EQ(r.at("metrics").at("releasedJobs").get<int>(), 4);
            CHECK_TRUE(!r.at("gantt").empty());
            CHECK_EQ(r.at("metrics").at("totalEvents").get<int>(),
                     static_cast<int>(r.at("trace").size()));
        }
    }

    // ---------- FIFO dispatch order = arrival/FIFO order ----------
    TEST("fifo-order");
    {
        micrort::Simulation sim(simOf(kFourTasks));
        const json r = sim.run();
        // A and B released at t=0 (declaration order), C at 2, D at 9.
        CHECK_EQ(dispatchOrder(r), "A,B,C,D");
    }

    // ---------- priority: higher priority wins at dispatch points ------
    TEST("priority-order");
    {
        json dom = json::parse(kFourTasks);
        dom["scheduler"] = json{{"type", "priority"}};
        Config cfg;
        std::vector<std::string> errors;
        micrort::parseConfig(dom, cfg, errors);
        micrort::validateConfig(cfg, errors);
        micrort::Simulation sim(cfg, dom);
        const json r = sim.run();
        CHECK_EQ(dispatchOrder(r), "B,C,A,D"); // B(5) first, then C(3),
                                               // A(1); D runs after A ends
    }

    // ---------- preemptive priority: D preempts A at t=9 ---------------
    TEST("priority-preempt");
    {
        json dom = json::parse(kFourTasks);
        dom["scheduler"] = json{{"type", "priority_p"}};
        Config cfg;
        std::vector<std::string> errors;
        micrort::parseConfig(dom, cfg, errors);
        micrort::validateConfig(cfg, errors);
        micrort::Simulation sim(cfg, dom);
        const json r = sim.run();
        CHECK_TRUE(r.at("metrics").at("preemptions").get<int>() >= 1);
        CHECK_TRUE(hasEventWhere(r, "PREEMPT", [](const json& e) {
            return e.at("task") == "A";
        }));
        // A runs 9-... gets preempted by D at 9, finishes after D.
        CHECK_TRUE(dispatchOrder(r).find("A,D,A") != std::string::npos);
    }

    // ---------- SRTF: shorter remaining preempts ------------------------
    TEST("srtf-preempt");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "srtf", "duration": 40,
          "scheduler": {"type": "srtf"},
          "tasks": [
            {"id": "LONG", "arrival": 0, "steps": [{"op": "cpu", "d": 10}]},
            {"id": "SHORT", "arrival": 3, "steps": [{"op": "cpu", "d": 2}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(dispatchOrder(r), "LONG,SHORT,LONG");
        CHECK_TRUE(r.at("metrics").at("preemptions").get<int>() >= 1);
    }

    // ---------- EDF: earliest deadline wins -----------------------------
    TEST("edf-order");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "edf", "duration": 40,
          "scheduler": {"type": "edf"},
          "tasks": [
            {"id": "FAR",  "arrival": 0, "relativeDeadline": 50,
             "steps": [{"op": "cpu", "d": 3}]},
            {"id": "NEAR", "arrival": 1, "relativeDeadline": 10,
             "steps": [{"op": "cpu", "d": 3}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        // FAR runs 0..1; NEAR arrives with an earlier deadline and
        // preempts; FAR resumes after NEAR completes.
        CHECK_EQ(dispatchOrder(r), "FAR,NEAR,FAR");
    }

    // ---------- RR rotation visible in gantt ---------------------------
    TEST("rr-quantum-rotation");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "rr", "duration": 30,
          "scheduler": {"type": "rr", "quantum": 2},
          "tasks": [
            {"id": "X", "arrival": 0, "steps": [{"op": "cpu", "d": 5}]},
            {"id": "Y", "arrival": 0, "steps": [{"op": "cpu", "d": 5}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(dispatchOrder(r), "X,Y,X,Y,X,Y");
        CHECK_TRUE(countEvents(r, "QUANTUM_EXPIRE") >= 4);
        // every RUNNING segment (note != "ctx") is <= quantum
        for (const json& g : r.at("gantt")) {
            if (g.at("state") == "RUNNING" && g.at("note").is_null()) {
                CHECK_TRUE(g.at("t1").get<int>() - g.at("t0").get<int>() <= 2);
            }
        }
    }

    // ---------- RR quantum extremes -------------------------------------
    TEST("rr-quantum-extremes");
    for (int q : {1, 1000}) {
        json dom = json::parse(kFourTasks);
        dom["scheduler"] = json{{"type", "rr"}, {"quantum", q}};
        Config cfg;
        std::vector<std::string> errors;
        micrort::parseConfig(dom, cfg, errors);
        micrort::validateConfig(cfg, errors);
        micrort::Simulation sim(cfg, dom);
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        CHECK_EQ(r.at("metrics").at("completedJobs").get<int>(), 4);
    }

    // ---------- context-switch cost accounting ---------------------------
    TEST("ctx-switch-cost");
    {
        json dom = json::parse(kFourTasks);
        dom["scheduler"] = json{{"type", "fifo"}};
        dom["contextSwitchCost"] = 2;
        Config cfg;
        std::vector<std::string> errors;
        micrort::parseConfig(dom, cfg, errors);
        micrort::validateConfig(cfg, errors);
        micrort::Simulation sim(cfg, dom);
        const json r = sim.run();
        const int switches = r.at("metrics").at("ctxSwitches").get<int>();
        CHECK_TRUE(switches >= 3); // A->B, B->C, C->D (D->A resume... >= 3)
        CHECK_EQ(r.at("metrics").at("ctxOverheadTime").get<int>(),
                 switches * 2);
        int ctxSegs = 0;
        for (const json& g : r.at("gantt")) {
            if (g.at("note") == "ctx") {
                ctxSegs++;
                CHECK_EQ(g.at("t1").get<int>() - g.at("t0").get<int>(), 2);
            }
        }
        CHECK_TRUE(ctxSegs >= 3);
        CHECK_EQ(countEvents(r, "CONTEXT_SWITCH"), switches);
    }

    // ---------- io / sleep -------------------------------------------------
    TEST("io-sleep");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "io", "duration": 40,
          "scheduler": {"type": "fifo"},
          "tasks": [
            {"id": "IO", "arrival": 0, "steps": [
              {"op": "cpu", "d": 2}, {"op": "io", "d": 5, "dev": "disk"},
              {"op": "cpu", "d": 1}]},
            {"id": "ZZ", "arrival": 1, "steps": [
              {"op": "sleep", "d": 3}, {"op": "cpu", "d": 1}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        CHECK_EQ(countEvents(r, "IO_START"), 1);
        CHECK_EQ(countEvents(r, "IO_END"), 1);
        CHECK_EQ(countEvents(r, "SLEEP_START"), 1);
        CHECK_EQ(countEvents(r, "SLEEP_END"), 1);
        // IO task: WAITING total = 5; sleep task: SLEEPING total = 3
        for (const json& t : r.at("tasks")) {
            if (t.at("id") == "IO") {
                CHECK_EQ(t.at("waitingTotal").get<int>(), 5);
            }
            if (t.at("id") == "ZZ") {
                CHECK_EQ(t.at("sleepingTotal").get<int>(), 3);
            }
        }
        bool sawWaiting = false, sawSleeping = false;
        for (const json& g : r.at("gantt")) {
            sawWaiting = sawWaiting || g.at("state") == "WAITING";
            sawSleeping = sawSleeping || g.at("state") == "SLEEPING";
        }
        CHECK_TRUE(sawWaiting);
        CHECK_TRUE(sawSleeping);
    }

    // ---------- mutex lock/unlock hand-off ---------------------------------
    TEST("mutex-handoff");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "mutex", "duration": 40,
          "scheduler": {"type": "priority_p"},
          "resources": [{"id": "M", "type": "mutex", "protocol": "none"}],
          "tasks": [
            {"id": "LOW", "priority": 1, "arrival": 0, "steps": [
              {"op": "cpu", "d": 1}, {"op": "lock", "res": "M"},
              {"op": "cpu", "d": 4}, {"op": "unlock", "res": "M"}]},
            {"id": "HIGH", "priority": 9, "arrival": 2, "steps": [
              {"op": "lock", "res": "M"}, {"op": "cpu", "d": 2},
              {"op": "unlock", "res": "M"}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        CHECK_EQ(countEvents(r, "LOCK_BLOCK"), 1);
        CHECK_TRUE(hasEventWhere(r, "LOCK_RELEASE", [](const json& e) {
            return e.at("detail").at("handedTo") == "HIGH";
        }));
        // HIGH blocked while LOW held M: blockedTotal >= 3
        for (const json& t : r.at("tasks")) {
            if (t.at("id") == "HIGH") {
                CHECK_TRUE(t.at("blockedTotal").get<int>() >= 3);
            }
        }
        CHECK_EQ(r.at("resources").at(0).at("finalOwner"), json());
    }

    // ---------- priority inheritance shortens HIGH's block -----------------
    TEST("priority-inheritance");
    {
        auto runVariant = [](const char* protocol) {
            json dom = json{
                {"schema", "micrort-config/1"},
                {"name", "pi"},
                {"duration", 60},
                {"scheduler", json{{"type", "priority_p"}}},
                {"resources",
                 json::array({json{{"id", "M"}, {"type", "mutex"},
                                   {"protocol", protocol}}})},
                {"tasks",
                 json::array({
                     json{{"id", "LOW"}, {"priority", 1}, {"arrival", 0},
                          {"steps", json::array(
                            {json{{"op", "cpu"}, {"d", 1}},
                             json{{"op", "lock"}, {"res", "M"}},
                             json{{"op", "cpu"}, {"d", 8}},
                             json{{"op", "unlock"}, {"res", "M"}}})}},
                     json{{"id", "MID"}, {"priority", 5}, {"arrival", 3},
                          {"steps", json::array(
                            {json{{"op", "cpu"}, {"d", 6}}})}},
                     json{{"id", "HIGH"}, {"priority", 9}, {"arrival", 2},
                          {"steps", json::array(
                            {json{{"op", "lock"}, {"res", "M"}},
                             json{{"op", "cpu"}, {"d", 2}},
                             json{{"op", "unlock"}, {"res", "M"}}})}}})}};
            Config cfg;
            std::vector<std::string> errors;
            micrort::parseConfig(dom, cfg, errors);
            micrort::validateConfig(cfg, errors);
            micrort::Simulation sim(cfg, dom);
            return sim.run();
        };
        const json plain = runVariant("none");
        const json inherit = runVariant("inherit");
        int plainBlocked = 0, inheritBlocked = 0;
        for (const json& t : plain.at("tasks")) {
            if (t.at("id") == "HIGH") {
                plainBlocked = t.at("blockedTotal").get<int>();
            }
        }
        for (const json& t : inherit.at("tasks")) {
            if (t.at("id") == "HIGH") {
                inheritBlocked = t.at("blockedTotal").get<int>();
            }
        }
        CHECK_TRUE(plainBlocked > 0);
        // Without inheritance MID preempts LOW (inversion): HIGH waits
        // LONGER than with inheritance (LOW boosted finishes sooner).
        CHECK_TRUE(plainBlocked > inheritBlocked);
        int inheritEvents = 0;
        for (const json& e : inherit.at("trace")) {
            if (e.at("type") == "LOCK_INHERIT") {
                inheritEvents++;
            }
        }
        CHECK_TRUE(inheritEvents >= 1);
    }

    // ---------- producer-consumer over msgq ---------------------------------
    TEST("producer-consumer");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "pc", "duration": 60,
          "scheduler": {"type": "fifo"},
          "resources": [{"id": "Q", "type": "msgq", "capacity": 2}],
          "tasks": [
            {"id": "CONS", "arrival": 0, "steps": [
              {"op": "recv", "res": "Q"}, {"op": "recv", "res": "Q"},
              {"op": "recv", "res": "Q"}]},
            {"id": "PROD", "arrival": 1, "steps": [
              {"op": "send", "res": "Q", "msg": 10},
              {"op": "send", "res": "Q", "msg": 20},
              {"op": "send", "res": "Q", "msg": 30}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        // CONS blocks on the empty queue before PROD's first send wakes it.
        CHECK_TRUE(hasEventWhere(r, "MSG_RECV", [](const json& e) {
            return e.at("task") == "CONS" &&
                   e.at("detail").at("blocked") == true;
        }));
        // payload order preserved: recv detail messages 10,20,30
        std::vector<int> got;
        for (const json& e : r.at("trace")) {
            if (e.at("type") == "MSG_RECV" &&
                !e.at("detail").at("msg").is_null()) {
                got.push_back(e.at("detail").at("msg").get<int>());
            }
        }
        CHECK_EQ(got.size(), static_cast<size_t>(3));
        if (got.size() == 3) {
            CHECK_EQ(got[0], 10);
            CHECK_EQ(got[1], 20);
            CHECK_EQ(got[2], 30);
        }
    }

    // ---------- semaphore token + overflow anomaly ---------------------------
    TEST("semantics-sem");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "sem", "duration": 40,
          "scheduler": {"type": "fifo"},
          "resources": [{"id": "S", "type": "sem", "initial": 0, "max": 1}],
          "tasks": [
            {"id": "T1", "arrival": 0, "steps": [
              {"op": "signal", "res": "S"}, {"op": "signal", "res": "S"}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        bool overflowAnomaly = false;
        for (const json& a : r.at("anomalies")) {
            if (a.at("type") == "semOverflow") {
                overflowAnomaly = true;
            }
        }
        CHECK_TRUE(overflowAnomaly);
        CHECK_TRUE(hasEventWhere(r, "SEM_SIGNAL", [](const json& e) {
            return e.at("detail").at("overflow") == true;
        }));
    }

    // ---------- event flags any/all ------------------------------------------
    TEST("event-flags");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "ev", "duration": 40,
          "scheduler": {"type": "fifo"},
          "resources": [{"id": "E", "type": "evflags"}],
          "tasks": [
            {"id": "W", "arrival": 0, "steps": [
              {"op": "evwait", "res": "E", "mask": 6, "mode": "all"}]},
            {"id": "S1", "arrival": 2, "steps": [
              {"op": "evset", "res": "E", "mask": 2}]},
            {"id": "S2", "arrival": 3, "steps": [
              {"op": "evset", "res": "E", "mask": 4}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
        // W wakes only when BOTH bits 2 and 4 are set (S2's set at t=3).
        CHECK_TRUE(hasEventWhere(r, "EV_WAIT", [](const json& e) {
            return e.at("task") == "W" &&
                   e.at("detail").at("satisfied") == false;
        }));
        CHECK_TRUE(hasEventWhere(r, "EV_SET", [](const json& e) {
            return e.at("task") == "S2" && e.at("detail").at("woken").size() == 1;
        }));
    }

    // ---------- memory alloc/free/leak/fragmentation -------------------------
    TEST("memory-model");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "mem", "duration": 40,
          "scheduler": {"type": "fifo"},
          "memory": {"model": "region", "total": 100, "policy": "first_fit"},
          "tasks": [
            {"id": "LEAK", "arrival": 0, "steps": [
              {"op": "alloc", "size": 30, "tag": "keep"},
              {"op": "cpu", "d": 1}]},
            {"id": "TIDY", "arrival": 2, "steps": [
              {"op": "alloc", "size": 20, "tag": "tmp"},
              {"op": "free", "tag": "tmp"},
              {"op": "cpu", "d": 1}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_EQ(countEvents(r, "MEM_ALLOC"), 2);
        CHECK_EQ(countEvents(r, "MEM_FREE"), 1);
        CHECK_EQ(r.at("memory").at("leaks").size(), static_cast<size_t>(1));
        CHECK_EQ(r.at("memory").at("leaks").at(0).at("task"), "LEAK");
        CHECK_EQ(r.at("memory").at("leaks").at(0).at("tag"), "keep");
        CHECK_EQ(r.at("memory").at("peakUsage").get<int>(), 50);
        CHECK_EQ(r.at("memory").at("finalLayout").at(0).at("owner"), "LEAK");
        CHECK_TRUE(r.at("memory").at("fragSeries").size() >= 3);
        CHECK_EQ(r.at("metrics").at("allocFailures").get<int>(), 0);
    }

    // ---------- deadline miss (EDF overload) -----------------------------------
    TEST("deadline-miss");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "dl", "duration": 50,
          "scheduler": {"type": "edf"},
          "tasks": [
            {"id": "OVER", "kind": "periodic", "arrival": 0, "period": 10,
             "relativeDeadline": 8,
             "steps": [{"op": "cpu", "d": 9}]}
          ]
        })JSON";
        micrort::Simulation sim(simOf(cfgText));
        const json r = sim.run();
        CHECK_TRUE(r.at("metrics").at("deadlineMisses").get<int>() >= 1);
        CHECK_TRUE(countEvents(r, "DEADLINE_MISS") >= 1);
        bool missAnomaly = false;
        for (const json& a : r.at("anomalies")) {
            if (a.at("type") == "deadlineMiss") {
                missAnomaly = true;
            }
        }
        CHECK_TRUE(missAnomaly);
    }

    // ---------- deadlock: circular wait ---------------------------------------
    TEST("deadlock-cycle");
    {
        const char* cfgText = R"JSON({
          "schema": "micrort-config/1", "name": "dl-cycle", "duration": 40,
          "scheduler": {"type": "fifo"},
          "resources": [
            {"id": "M1", "type": "mutex", "protocol": "none"},
            {"id": "M2", "type": "mutex", "protocol": "none"}],
          "tasks": [
            {"id": "A", "arrival": 0, "steps": [
              {"op": "lock", "res": "M1"}, {"op": "cpu", "d": 2},
              {"op": "lock", "res": "M2"}]},
            {"id": "B", "arrival": 0, "steps": [
              {"op": "lock", "res": "M2"}, {"op": "cpu", "d": 2},
              {"op": "lock", "res": "M1"}]}
          ]
        })JSON";
        // RR quantum 1 interleaves A and B so each holds its first mutex
        // before the other requests it — the classic circular wait.
        json dom = json::parse(cfgText);
        dom["scheduler"] = json{{"type", "rr"}, {"quantum", 1}};
        Config cfg;
        std::vector<std::string> errors;
        CHECK_TRUE(micrort::parseConfig(dom, cfg, errors) &&
                   micrort::validateConfig(cfg, errors));
        micrort::Simulation sim(cfg, dom);
        const json r = sim.run();
        CHECK_TRUE(r.at("deadlocks").size() >= 1);
        CHECK_TRUE(countEvents(r, "DEADLOCK") >= 1);
        if (r.at("deadlocks").size() >= 1) {
            const json& d = r.at("deadlocks").at(0);
            // cycle alternates task/resource ids
            CHECK_EQ(d.at("tasks").size(), static_cast<size_t>(2));
            CHECK_EQ(d.at("resources").size(), static_cast<size_t>(2));
        }
        bool deadlocked = false;
        for (const json& a : r.at("anomalies")) {
            if (a.at("type") == "deadlock") {
                deadlocked = true;
            }
        }
        CHECK_TRUE(deadlocked);
        // tasks remain BLOCKED at end; run still ends (status completed)
        CHECK_EQ(r.at("status").get<std::string>(), "completed");
    }

    // ---------- edge cases ------------------------------------------------------
    TEST("edge-cases");
    {
        // zero tasks
        {
            const char* zero = R"JSON({
              "schema": "micrort-config/1", "name": "zero", "duration": 10,
              "scheduler": {"type": "fifo"}, "tasks": []
            })JSON";
            micrort::Simulation sim(simOf(zero));
            const json r = sim.run();
            CHECK_EQ(r.at("status").get<std::string>(), "completed");
            CHECK_TRUE(r.at("tasks").empty());
            CHECK_TRUE(r.at("trace").size() >= 2); // SIM_START + SIM_END
        }
        // one task
        {
            const char* one = R"JSON({
              "schema": "micrort-config/1", "name": "one", "duration": 10,
              "scheduler": {"type": "rr", "quantum": 2},
              "tasks": [{"id": "ONLY", "steps": [{"op": "cpu", "d": 3}]}]
            })JSON";
            micrort::Simulation sim(simOf(one));
            const json r = sim.run();
            CHECK_EQ(r.at("status").get<std::string>(), "completed");
            CHECK_EQ(r.at("metrics").at("completedJobs").get<int>(), 1);
        }
        // identical priorities: FIFO ties by declaration order
        {
            json dom = json::parse(kFourTasks);
            for (json& t : dom.at("tasks")) {
                t["priority"] = 5;
            }
            dom["scheduler"] = json{{"type", "priority_p"}};
            Config cfg;
            std::vector<std::string> errors;
            micrort::parseConfig(dom, cfg, errors);
            micrort::validateConfig(cfg, errors);
            micrort::Simulation sim(cfg, dom);
            const json r = sim.run();
            CHECK_EQ(dispatchOrder(r), "A,B,C,D");
        }
        // task arriving mid-burst (fifo: no preemption)
        {
            const char* mid = R"JSON({
              "schema": "micrort-config/1", "name": "mid", "duration": 30,
              "scheduler": {"type": "fifo"},
              "tasks": [
                {"id": "FIRST", "steps": [{"op": "cpu", "d": 10}]},
                {"id": "LATE", "arrival": 5, "steps": [{"op": "cpu", "d": 2}]}
              ]
            })JSON";
            micrort::Simulation sim(simOf(mid));
            const json r = sim.run();
            CHECK_EQ(dispatchOrder(r), "FIRST,LATE");
        }
        // skipped release: job outlives period
        {
            const char* skip = R"JSON({
              "schema": "micrort-config/1", "name": "skip", "duration": 30,
              "scheduler": {"type": "fifo"},
              "tasks": [
                {"id": "SLOW", "kind": "periodic", "period": 5,
                 "steps": [{"op": "cpu", "d": 9}]}
              ]
            })JSON";
            micrort::Simulation sim(simOf(skip));
            const json r = sim.run();
            bool skipped = false;
            for (const json& a : r.at("anomalies")) {
                if (a.at("type") == "skippedRelease") {
                    skipped = true;
                }
            }
            CHECK_TRUE(skipped);
            CHECK_TRUE(r.at("metrics").at("releasedJobs").get<int>() <
                       30 / 5);
        }
    }

    // ---------- determinism -----------------------------------------------------
    TEST("determinism");
    {
        const std::string texts[] = {kFixtureSmoke, kFourTasks};
        for (const std::string& txt : texts) {
            json dom = json::parse(txt);
            Config cfg;
            std::vector<std::string> errors;
            micrort::parseConfig(dom, cfg, errors);
            micrort::validateConfig(cfg, errors);
            micrort::Simulation s1(cfg, dom);
            micrort::Simulation s2(cfg, dom);
            json a = s1.run();
            json b = s2.run();
            a.erase("wallMicros");
            b.erase("wallMicros");
            CHECK_TRUE(a == b);
        }
    }

    return TEST_SUMMARY();
}
