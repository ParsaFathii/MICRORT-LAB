// test_metrics.cpp — Python-style recomputation of metrics from trace+gantt
// must match the engine's reported metrics (schema §Metric formulas).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0.
#define TH_SUITE "test_metrics"
#include "test_harness.hpp"
#include "test_fixtures.hpp"

#include <micrort/config.hpp>
#include <micrort/simulation.hpp>

using nlohmann::json;
using micrort::Config;

namespace {

json runCfg(const char* text) {
    json dom = json::parse(text);
    Config cfg;
    std::vector<std::string> errors;
    CHECK_TRUE(micrort::parseConfig(dom, cfg, errors) &&
               micrort::validateConfig(cfg, errors));
    micrort::Simulation sim(cfg, dom);
    return sim.run();
}

// Engine-reported averages are rounded to 3 decimals (schema), so the
// recomputation tolerance covers the rounding bound (0.0005) plus margin.
bool near(double a, double b, double eps = 1.5e-3) {
    return std::fabs(a - b) < eps;
}

// Recompute waiting / turnaround / response per job from the gantt +
// TASK_ARRIVAL/JOB_RELEASE/TASK_COMPLETE/DISPATCH trace records — the
// exact derivation the Python analysis layer performs.
struct JobStats {
    std::int64_t release = -1;
    std::int64_t firstDispatch = -1;
    std::int64_t completion = -1;
    std::int64_t readyTime = 0;
};

} // namespace

int main() {
    const char* workloads[] = {
        R"JSON({
          "schema": "micrort-config/1", "name": "m1", "duration": 60,
          "contextSwitchCost": 1,
          "scheduler": {"type": "rr", "quantum": 3},
          "tasks": [
            {"id": "A", "priority": 2, "arrival": 0, "steps": [
              {"op": "cpu", "d": 7}, {"op": "io", "d": 3},
              {"op": "cpu", "d": 2}]},
            {"id": "B", "priority": 5, "arrival": 1, "steps": [
              {"op": "cpu", "d": 4}, {"op": "sleep", "d": 2},
              {"op": "cpu", "d": 3}]},
            {"id": "C", "priority": 3, "arrival": 3, "steps": [
              {"op": "cpu", "d": 5}]}
          ]
        })JSON",
        R"JSON({
          "schema": "micrort-config/1", "name": "m2", "duration": 70,
          "scheduler": {"type": "edf"},
          "resources": [{"id": "M", "type": "mutex", "protocol": "inherit"}],
          "tasks": [
            {"id": "P", "kind": "periodic", "arrival": 0, "period": 20,
             "relativeDeadline": 18, "steps": [
              {"op": "cpu", "d": 4}, {"op": "lock", "res": "M"},
              {"op": "cpu", "d": 3}, {"op": "unlock", "res": "M"}]},
            {"id": "Q", "kind": "periodic", "arrival": 5, "period": 30,
             "relativeDeadline": 25, "steps": [
              {"op": "lock", "res": "M"}, {"op": "cpu", "d": 4},
              {"op": "unlock", "res": "M"}, {"op": "cpu", "d": 2}]},
            {"id": "SP", "kind": "sporadic", "priority": 1,
             "releases": [2, 25, 50], "steps": [{"op": "cpu", "d": 3}]}
          ]
        })JSON"};

    for (const char* wl : workloads) {
        const json r = runCfg(wl);
        TEST("metrics-recompute");
        {
            // ---- per-job stats from trace ----
            // job identity: (task, job ordinal) from TASK_ARRIVAL/JOB_RELEASE
            // order; DISPATCH gives firstDispatch; TASK_COMPLETE completion.
            std::map<std::string, std::vector<JobStats>> jobs;
            std::map<std::string, int> jobCursor;
            for (const json& e : r.at("trace")) {
                const std::string type = e.at("type").get<std::string>();
                if (!e.at("task").is_string()) {
                    continue;
                }
                const std::string task =
                    e.at("task").get<std::string>();
                if (type == "TASK_ARRIVAL" || type == "JOB_RELEASE") {
                    JobStats js;
                    js.release = e.at("t").get<std::int64_t>();
                    jobs[task].push_back(js);
                } else if (type == "DISPATCH") {
                    auto& v = jobs[task];
                    if (!v.empty() && v.back().firstDispatch < 0) {
                        v.back().firstDispatch =
                            e.at("t").get<std::int64_t>();
                    }
                } else if (type == "TASK_COMPLETE") {
                    auto& v = jobs[task];
                    if (!v.empty()) {
                        v.back().completion =
                            e.at("t").get<std::int64_t>();
                    }
                }
            }
            // READY time per job from gantt
            for (const json& g : r.at("gantt")) {
                if (g.at("state") != "READY") {
                    continue;
                }
                auto& v = jobs[g.at("task").get<std::string>()];
                // attribute to the job active in [t0,t1): the last job
                // whose release <= t0 and not yet completed at t0
                const std::int64_t t0 = g.at("t0").get<std::int64_t>();
                const std::int64_t len =
                    g.at("t1").get<std::int64_t>() - t0;
                for (auto it = v.rbegin(); it != v.rend(); ++it) {
                    if (it->release >= 0 && it->release <= t0 &&
                        (it->completion < 0 || it->completion >= t0)) {
                        it->readyTime += len;
                        break;
                    }
                }
            }
            // aggregate
            std::vector<std::int64_t> wait, turn, resp;
            int completedJobs = 0;
            for (const auto& [task, v] : jobs) {
                for (const JobStats& js : v) {
                    // response counts every DISPATCHED job (schema:
                    // response = firstDispatch - release), even when the
                    // job is still unfinished at the horizon.
                    if (js.firstDispatch >= 0) {
                        resp.push_back(js.firstDispatch - js.release);
                    }
                    if (js.completion < 0) {
                        continue;
                    }
                    completedJobs++;
                    wait.push_back(js.readyTime);
                    turn.push_back(js.completion - js.release);
                }
            }
            auto mean = [](const std::vector<std::int64_t>& xs) {
                if (xs.empty()) {
                    return 0.0;
                }
                double s = 0;
                for (std::int64_t x : xs) {
                    s += static_cast<double>(x);
                }
                return s / static_cast<double>(xs.size());
            };
            const json& m = r.at("metrics");
            CHECK_TRUE(near(m.at("avgWaiting").get<double>(), mean(wait)));
            CHECK_TRUE(near(m.at("avgTurnaround").get<double>(), mean(turn)));
            CHECK_TRUE(near(m.at("avgResponse").get<double>(), mean(resp)));
            CHECK_EQ(m.at("completedJobs").get<int>(), completedJobs);

            // ---- per-task gantt aggregates vs task blocks ----
            std::map<std::string, std::int64_t> cpuT, readyT, blockT,
                waitT, sleepT;
            for (const json& g : r.at("gantt")) {
                std::int64_t len =
                    g.at("t1").get<std::int64_t>() - g.at("t0").get<std::int64_t>();
                const std::string st = g.at("state").get<std::string>();
                const std::string task = g.at("task").get<std::string>();
                if (st == "RUNNING") {
                    if (g.at("note").is_null()) {
                        cpuT[task] += len;
                    }
                } else if (st == "READY") {
                    readyT[task] += len;
                } else if (st == "BLOCKED") {
                    blockT[task] += len;
                } else if (st == "WAITING") {
                    waitT[task] += len;
                } else if (st == "SLEEPING") {
                    sleepT[task] += len;
                }
            }
            for (const json& t : r.at("tasks")) {
                const std::string id = t.at("id").get<std::string>();
                CHECK_EQ(t.at("cpuTime").get<std::int64_t>(), cpuT[id]);
                CHECK_EQ(t.at("readyWaitTotal").get<std::int64_t>(),
                         readyT[id]);
                CHECK_EQ(t.at("blockedTotal").get<std::int64_t>(), blockT[id]);
                CHECK_EQ(t.at("waitingTotal").get<std::int64_t>(), waitT[id]);
                CHECK_EQ(t.at("sleepingTotal").get<std::int64_t>(),
                         sleepT[id]);
            }

            // ---- cpu utilization / idle / throughput ----
            std::int64_t busy = 0;
            for (const json& g : r.at("gantt")) {
                if (g.at("state") == "RUNNING") { // includes ctx zones
                    busy += g.at("t1").get<std::int64_t>() -
                            g.at("t0").get<std::int64_t>();
                }
            }
            const std::int64_t horizon =
                r.at("simulatedUntil").get<std::int64_t>();
            CHECK_TRUE(near(m.at("cpuUtilization").get<double>(),
                            static_cast<double>(busy) /
                                static_cast<double>(horizon)));
            CHECK_TRUE(near(m.at("idleTime").get<std::int64_t>(),
                            horizon - busy));
            CHECK_TRUE(near(m.at("throughput").get<double>(),
                            static_cast<double>(completedJobs) /
                                static_cast<double>(horizon)));

            // ---- context switch accounting ----
            CHECK_EQ(m.at("ctxOverheadTime").get<std::int64_t>(),
                     m.at("ctxSwitches").get<std::int64_t>() *
                         r.at("config").value("contextSwitchCost", 0));
            int ctxTrace = 0;
            for (const json& e : r.at("trace")) {
                if (e.at("type") == "CONTEXT_SWITCH") {
                    ctxTrace++;
                }
            }
            CHECK_EQ(m.at("ctxSwitches").get<std::int64_t>(),
                     static_cast<std::int64_t>(ctxTrace));
        }
    }

    return TEST_SUMMARY();
}
