// result.cpp — result document assembly (schema micrort-result/1).
//
// buildResultDoc() is a friend of Simulation and reads the finished run
// state only. Every field of docs/spec/SIMULATION_SCHEMA.md §Result
// document is emitted, empty arrays are always present, absent scalars
// are null and averages round to 3 decimals. Output-affecting paths use
// declaration order / std::map only, so identical (config, seed) input
// produces a byte-identical document except `wallMicros`.
//
// Metric sources (schema §Metric formulas):
//   per-task aggregates come from the engine's gantt-segment accounting
//   (TaskRt), which the Python layer can recompute from trace+gantt —
//   the cross-check is asserted by engine/tests/test_metrics.cpp.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0.
#include <micrort/result.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <micrort/sha256.hpp>
#include <micrort/simulation.hpp>

namespace micrort {

using nlohmann::json;

namespace {

double round3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

json meanOf(const std::vector<std::int64_t>& xs) {
    if (xs.empty()) {
        return json(); // null: no completed jobs
    }
    double s = 0.0;
    for (const std::int64_t x : xs) {
        s += static_cast<double>(x);
    }
    return round3(s / static_cast<double>(xs.size()));
}

json timeOrMrtMax(std::int64_t t, std::int64_t unset) {
    return (t == unset || t < 0) ? json() : json(t);
}

} // namespace

json buildResultDoc(const Simulation& sim, std::uint64_t wallMicros) {
    const Config& cfg = sim.cfg_;
    const std::vector<TaskRt>& rt = sim.rt_;
    const mrt_kernel_t& k = sim.k_;

    // ---- header ----------------------------------------------------
    json doc;
    doc["schema"] = "micrort-result/1";
    doc["config"] = sim.originalConfig_;
    doc["configHash"] = sha256_hex(canonicalJson(sim.originalConfig_))
                            .substr(0, 16);
    doc["seed"] = cfg.seed;
    doc["status"] = sim.status_;
    doc["simulatedUntil"] = sim.endTick_;
    doc["wallMicros"] = wallMicros;
    {
        json sch;
        sch["type"] = schedTypeName(cfg.scheduler.type);
        sch["quantum"] = (cfg.scheduler.type == SchedType::RR)
                             ? json(cfg.scheduler.quantum)
                             : json();
        doc["scheduler"] = sch;
    }

    // ---- tasks ------------------------------------------------------
    json tasks = json::array();
    std::int64_t blockedTotalAll = 0;
    for (std::size_t i = 0; i < cfg.tasks.size(); ++i) {
        const TaskCfg& tc = cfg.tasks[i];
        const TaskRt& r = rt[i];
        const mrt_task_t* tcb = mrt_task_get(
            const_cast<mrt_kernel_t*>(&k), r.kid);

        json t;
        t["id"] = tc.id;
        t["name"] = tc.name.empty() ? tc.id : tc.name;
        t["kind"] = taskKindName(tc.kind);
        // The CONFIGURED priority (RM/FIFO/RR may remap kernel-internal
        // priorities; the document reports the experiment as authored).
        t["priority"] = tc.priority;
        t["arrival"] = tc.arrival;
        t["period"] = (tc.kind == TaskKind::Periodic) ? json(tc.period)
                                                      : json();
        t["relativeDeadline"] = (tc.relativeDeadline > 0)
                                    ? json(tc.relativeDeadline)
                                    : json();
        t["jobsReleased"] = (tcb != nullptr) ? json(tcb->jobs_released)
                                             : json(0);
        t["jobsCompleted"] = (tcb != nullptr) ? json(tcb->jobs_completed)
                                              : json(0);
        t["deadlineMisses"] = (tcb != nullptr) ? json(tcb->deadline_misses)
                                               : json(0);
        t["cpuTime"] = r.cpuTime;
        t["readyWaitTotal"] = r.readyWaitTotal;
        t["blockedTotal"] = r.blockedTotal;
        t["waitingTotal"] = r.waitingTotal;
        t["sleepingTotal"] = r.sleepingTotal;
        t["firstStart"] = timeOrMrtMax(
            static_cast<std::int64_t>(r.firstStart),
            static_cast<std::int64_t>(MRT_TIME_MAX));
        t["lastFinish"] = timeOrMrtMax(
            static_cast<std::int64_t>(r.lastFinish),
            static_cast<std::int64_t>(MRT_TIME_MAX));
        json resp = json::array();
        for (const std::int64_t v : r.responseTimes) resp.push_back(v);
        json turn = json::array();
        for (const std::int64_t v : r.turnaroundTimes) turn.push_back(v);
        json wait = json::array();
        for (const std::int64_t v : r.waitTimes) wait.push_back(v);
        t["responseTimes"] = resp;
        t["turnaroundTimes"] = turn;
        t["waitTimes"] = wait;
        // Starvation heuristic (schema): long READY residence with little
        // or no CPU progress while other tasks ran.
        bool anyRan = false;
        for (const TaskRt& o : rt) {
            if (o.cpuTime > 0) {
                anyRan = true;
                break;
            }
        }
        t["starved"] =
            (anyRan && r.readyWaitTotal >= 100 &&
             (r.cpuTime == 0 || r.readyWaitTotal >= 5 * r.cpuTime));
        blockedTotalAll += r.blockedTotal;
        tasks.push_back(t);
    }
    doc["tasks"] = tasks;

    // ---- metrics ----------------------------------------------------
    json m;
    std::vector<std::int64_t> allWait, allTurn, allResp;
    for (const TaskRt& r : rt) {
        allWait.insert(allWait.end(), r.waitTimes.begin(),
                       r.waitTimes.end());
        allTurn.insert(allTurn.end(), r.turnaroundTimes.begin(),
                       r.turnaroundTimes.end());
        allResp.insert(allResp.end(), r.responseTimes.begin(),
                       r.responseTimes.end());
    }
    m["avgWaiting"] = meanOf(allWait);
    m["avgTurnaround"] = meanOf(allTurn);
    m["avgResponse"] = meanOf(allResp);
    const std::int64_t horizon =
        (sim.endTick_ > 0) ? static_cast<std::int64_t>(sim.endTick_) : 1;
    m["throughput"] = round3(static_cast<double>(sim.completedJobs_) /
                             static_cast<double>(horizon));
    m["completedJobs"] = sim.completedJobs_;
    m["releasedJobs"] = sim.releasedJobs_;
    m["completionRate"] =
        (sim.releasedJobs_ == 0)
            ? round3(1.0)
            : round3(static_cast<double>(sim.completedJobs_) /
                     static_cast<double>(sim.releasedJobs_));
    m["cpuUtilization"] = round3(
        static_cast<double>(sim.busyRunTime_) /
        static_cast<double>(horizon));
    m["idleTime"] = sim.idleTime_;
    m["ctxSwitches"] = sim.ctxSwitches_;
    m["preemptions"] = sim.preemptions_;
    m["ctxOverheadTime"] = k.ctx_overhead_time;
    m["avgQueueLen"] = round3(
        static_cast<double>(sim.qIntegral_) / static_cast<double>(horizon));
    m["maxQueueLen"] = sim.qMax_;
    m["deadlineMisses"] = sim.deadlineMisses_;
    m["blockedTimeTotal"] = blockedTotalAll;
    m["memoryUtilizationAvg"] =
        (sim.memUtilSamples_ > 0 && cfg.memory.enabled)
            ? round3(sim.memUtilSum_ /
                     static_cast<double>(sim.memUtilSamples_))
            : 0.0;
    m["memoryPeak"] = cfg.memory.enabled ? json(sim.memPeak_) : json(0);
    m["fragmentationAvg"] =
        (sim.memUtilSamples_ > 0 && cfg.memory.enabled)
            ? round3(static_cast<double>(sim.fragSum_) /
                     static_cast<double>(sim.memUtilSamples_))
            : 0.0;
    m["fragmentationMax"] = cfg.memory.enabled ? json(sim.fragMax_)
                                               : json(0);
    m["allocFailures"] = sim.memFailures_.size();
    m["totalEvents"] = sim.totalEvents_;
    doc["metrics"] = m;

    // ---- trace / gantt ----------------------------------------------
    std::vector<std::string> taskIds;
    taskIds.reserve(cfg.tasks.size());
    for (const TaskCfg& tc : cfg.tasks) {
        taskIds.push_back(tc.id);
    }
    std::vector<std::string> resIds;
    resIds.reserve(cfg.resources.size());
    for (const ResCfg& rc : cfg.resources) {
        resIds.push_back(rc.id);
    }
    doc["trace"] = sim.trace_.toJson(taskIds, resIds);

    json gantt = json::array();
    for (const GanttSeg& g : sim.gantt_) {
        json s;
        s["task"] = taskIds[static_cast<std::size_t>(g.taskIdx)];
        s["t0"] = g.t0;
        s["t1"] = g.t1;
        s["state"] = g.state;
        s["note"] = g.note != nullptr ? json(g.note) : json();
        gantt.push_back(s);
    }
    doc["gantt"] = gantt;

    // ---- resources ---------------------------------------------------
    json resources = json::array();
    for (std::size_t i = 0; i < cfg.resources.size(); ++i) {
        const ResCfg& rc = cfg.resources[i];
        const mrt_resource_t* res = mrt_res_get(
            const_cast<mrt_kernel_t*>(&k), static_cast<std::uint32_t>(i));
        json r;
        r["id"] = rc.id;
        r["type"] = resKindName(rc.kind);
        if (rc.kind == ResKind::Mutex && res != nullptr &&
            res->owner != MRT_TASK_ID_NONE) {
            const std::string owner =
                taskIds[static_cast<std::size_t>(
                    sim.kidToIdx_[res->owner])];
            r["finalOwner"] = owner;
            r["holderAtEnd"] = owner;
        } else {
            r["finalOwner"] = json();
            r["holderAtEnd"] = json();
        }
        r["acquisitions"] = (res != nullptr) ? json(res->acquisitions)
                                             : json(0);
        r["contentions"] = (res != nullptr) ? json(res->contentions)
                                            : json(0);
        r["queueDepth"] =
            (res != nullptr) ? json(res->waiter_count) : json(0);
        resources.push_back(r);
    }
    doc["resources"] = resources;

    // ---- deadlocks ----------------------------------------------------
    json deadlocks = json::array();
    for (const DeadlockRec& d : sim.deadlocks_) {
        json r;
        r["t"] = d.t;
        json cyc = json::array();
        for (const std::int32_t e : d.cycle) {
            cyc.push_back(e);
        }
        r["cycle"] = cyc;
        json tk = json::array();
        for (const std::int32_t e : d.tasks) {
            tk.push_back(taskIds[static_cast<std::size_t>(e)]);
        }
        r["tasks"] = tk;
        json rs = json::array();
        for (const std::int32_t e : d.resources) {
            rs.push_back(resIds[static_cast<std::size_t>(e)]);
        }
        r["resources"] = rs;
        deadlocks.push_back(r);
    }
    doc["deadlocks"] = deadlocks;

    // ---- memory ---------------------------------------------------------
    json mem;
    if (cfg.memory.enabled) {
        mem["model"] = memModelName(cfg.memory.model);
        mem["total"] = cfg.memory.total;
        mem["policy"] =
            (cfg.memory.model == MemModel::Pool)
                ? json()
                : json(memPolicyName(cfg.memory.policy));
        json evs = json::array();
        for (const MemEventRec& e : sim.memEvents_) {
            json ev;
            ev["t"] = e.t;
            ev["op"] = e.alloc ? "alloc" : "free";
            ev["task"] = (e.taskIdx >= 0)
                             ? json(taskIds[static_cast<std::size_t>(
                                   e.taskIdx)])
                             : json();
            ev["tag"] = e.tag;
            ev["size"] = e.size;
            ev["result"] = e.ok ? "ok" : "failed";
            ev["offset"] = (e.offset >= 0) ? json(e.offset) : json();
            evs.push_back(ev);
        }
        mem["events"] = evs;
        json layout = json::array();
        for (const MemLayoutRec& l : sim.memLayout_) {
            json b;
            b["offset"] = l.offset;
            b["size"] = l.size;
            b["owner"] = (l.taskIdx >= 0)
                             ? json(taskIds[static_cast<std::size_t>(
                                   l.taskIdx)])
                             : json();
            if (l.hasTag) {
                b["tag"] = l.tag;
            } else if (l.taskIdx < 0) {
                b["tag"] = json();
            } else {
                b["tag"] = json();
            }
            layout.push_back(b);
        }
        mem["finalLayout"] = layout;
        json fails = json::array();
        for (const MemEventRec& e : sim.memFailures_) {
            json f;
            f["t"] = e.t;
            f["task"] = (e.taskIdx >= 0)
                            ? json(taskIds[static_cast<std::size_t>(
                                  e.taskIdx)])
                            : json();
            f["tag"] = e.tag;
            f["size"] = e.size;
            f["offset"] = (e.offset >= 0) ? json(e.offset) : json();
            fails.push_back(f);
        }
        mem["failures"] = fails;
        json leaks = json::array();
        for (const auto& lk : sim.leaks_) {
            json l;
            l["task"] = taskIds[static_cast<std::size_t>(lk.taskIdx)];
            l["tag"] = lk.tag;
            l["offset"] = lk.offset;
            l["size"] = lk.size;
            leaks.push_back(l);
        }
        mem["leaks"] = leaks;
        mem["peakUsage"] = sim.memPeak_;
        json series = json::array();
        for (const FragPoint& p : sim.fragSeries_) {
            json pt;
            pt["t"] = p.t;
            pt["used"] = p.used;
            pt["free"] = p.free;
            pt["largest"] = p.largest;
            pt["frag"] = p.frag;
            series.push_back(pt);
        }
        mem["fragSeries"] = series;
    } else {
        mem["model"] = "none";
        mem["total"] = 0;
        mem["policy"] = json();
        mem["events"] = json::array();
        mem["finalLayout"] = json::array();
        mem["failures"] = json::array();
        mem["leaks"] = json::array();
        mem["peakUsage"] = 0;
        mem["fragSeries"] = json::array();
    }
    doc["memory"] = mem;

    // ---- anomalies --------------------------------------------------------
    json anom = json::array();
    for (const Anomaly& a : sim.anomalies_) {
        json e;
        e["t"] = a.t;
        e["type"] = a.type;
        json tk = json::array();
        if (a.taskIdx >= 0) {
            tk.push_back(taskIds[static_cast<std::size_t>(a.taskIdx)]);
        }
        e["tasks"] = tk;
        json rs = json::array();
        if (a.resIdx >= 0) {
            rs.push_back(resIds[static_cast<std::size_t>(a.resIdx)]);
        }
        e["resources"] = rs;
        e["detail"] = a.detail;
        anom.push_back(e);
    }
    doc["anomalies"] = anom;

    // ---- scheduler notes ---------------------------------------------------
    json notes = json::array();
    for (const SchedNote& n : sim.schedNotes_) {
        json e;
        e["t"] = n.t;
        e["chosen"] = (n.taskIdx >= 0)
                          ? json(taskIds[static_cast<std::size_t>(n.taskIdx)])
                          : json();
        e["reason"] = n.reason;
        notes.push_back(e);
    }
    doc["schedulerNotes"] = notes;

    return doc;
}

} // namespace micrort
