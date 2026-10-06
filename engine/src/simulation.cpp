// simulation.cpp — the MicroRT-Lab discrete-event core (stage 2-b2).
//
// Event loop, program interpretation, gantt building, memory model driving
// and a basic mutex-RAG deadlock detector. Every policy decision that is
// not spelled out by SIMULATION_SCHEMA.md is documented here:
//
//  * Context switches are charged whenever a dispatch picks a task that
//    differs from the cpu's most recent occupant (preemption, rotation,
//    completion, block, or dispatch after an idle gap); the very first
//    dispatch of the run and a quantum self-rotation are free.
//  * A periodic/sporadic release that fires while the task still has an
//    active (unfinished) job is SKIPPED with a "skippedRelease" anomaly —
//    jobs never overlap.
//  * Deadline verdicts are deferred to the END of the deadline tick: a job
//    completing exactly AT its deadline is on time.
//  * The gantt "ctx" zone is a RUNNING segment of the INCOMING task
//    (schema example), so cpuUtilization includes context-switch overhead.
//    Per-task cpuTime counts only note-free RUNNING segments.
//  * task.readyWait / job wait time end at the DISPATCH decision instant
//    (that is where the gantt READY segment closes).
//  * Aging is armed only under priority / priority_p / rr (schema: aging
//    is effective for the priority* family; RM must stay static).
//  * msgq wakes re-execute the send/recv step on the woken task's next
//    dispatch (kernel contract: the payload stays queued); mutex/sem/
//    evflags wakes complete the blocking step at wake time.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#include <micrort/simulation.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <utility>

#include <micrort/result.hpp>
#include <micrort/rng.hpp>

namespace micrort {

using nlohmann::json;

namespace {

bool sameStr(const char* a, const char* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    return std::strcmp(a, b) == 0;
}

const char* kReasonIdle = "idle";
const char* kReasonPreempt = "preempt";
const char* kReasonRotate = "rotate";
const char* kCtxNote = "ctx";

} // namespace

// ---------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------

Simulation::Simulation(const Config& cfg, const json& originalConfig)
    : cfg_(cfg), originalConfig_(originalConfig),
      sched_(makeScheduler(cfg.scheduler)), rng_(cfg.seed),
      trace_(TraceRecorder::kMaxEvents) {}

Simulation::~Simulation() = default;

json Simulation::run() {
    if (ran_) {
        return buildResultDoc(*this, 0);
    }
    ran_ = true;
    const auto t0 = std::chrono::steady_clock::now();
    initKernel();
    while (advanceTime()) {
        pendingDl_.clear();
        processDueTimers();
        processEventsAt(now_);
        scheduleTick(now_);
        finishTick(now_);
    }
    finalize();
    const auto t1 = std::chrono::steady_clock::now();
    const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(
                          t1 - t0)
                          .count();
    return buildResultDoc(*this, static_cast<std::uint64_t>(wall));
}

// ---------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------

void Simulation::initKernel() {
    mrt_kernel_init(&k_);

    const SchedType st = cfg_.scheduler.type;

    // Aging: priority*/rr only (RM stays static per scheduler.hpp).
    agingOn_ = cfg_.aging.enabled &&
               (st == SchedType::Priority || st == SchedType::PriorityP ||
                st == SchedType::RR);
    if (agingOn_ && cfg_.aging.interval > 0) {
        k_.aging_interval = static_cast<mrt_time_t>(cfg_.aging.interval);
        k_.aging_cap = static_cast<mrt_prio_t>(cfg_.aging.cap);
        std::uint32_t h = 0;
        (void)mrt_timer_add(&k_.timers,
                            static_cast<mrt_time_t>(cfg_.aging.interval),
                            MRT_TASK_ID_NONE, MRT_TIMER_TAG_AGING, &h);
    }

    // Memory model.
    if (cfg_.memory.enabled) {
        if (cfg_.memory.model == MemModel::Pool) {
            mrt_pool_init(&k_.pool,
                          static_cast<std::size_t>(cfg_.memory.blockSize),
                          static_cast<std::size_t>(cfg_.memory.blockCount));
        } else {
            const mrt_fit_policy_t pol =
                cfg_.memory.policy == MemPolicy::Best
                    ? MRT_FIT_BEST
                    : (cfg_.memory.policy == MemPolicy::Worst ? MRT_FIT_WORST
                                                              : MRT_FIT_FIRST);
            mrt_mem_init(&k_.region,
                         static_cast<std::size_t>(cfg_.memory.total), pol);
        }
    }

    // Resources (declaration order == kernel order).
    std::map<std::string, int> resIdxById;
    for (std::size_t i = 0; i < cfg_.resources.size(); ++i) {
        const ResCfg& rc = cfg_.resources[i];
        std::uint32_t ri = 0;
        switch (rc.kind) {
        case ResKind::Mutex:
            (void)mrt_res_register_mutex(
                &k_, rc.id.c_str(),
                rc.protocol == ResProtocol::Inherit ? MRT_PROTO_INHERIT
                                                   : MRT_PROTO_NONE,
                &ri);
            break;
        case ResKind::Sem:
            (void)mrt_res_register_sem(&k_, rc.id.c_str(),
                                       static_cast<std::int32_t>(rc.initial),
                                       static_cast<std::int32_t>(rc.max), &ri);
            break;
        case ResKind::MsgQ: {
            msgqBufs_.emplace_back(
                static_cast<std::size_t>(rc.capacity), 0);
            (void)mrt_res_register_msgq(
                &k_, rc.id.c_str(), static_cast<std::int32_t>(rc.capacity),
                msgqBufs_.back().data(), &ri);
            break;
        }
        case ResKind::EvFlags:
            (void)mrt_res_register_evflags(&k_, rc.id.c_str(), &ri);
            break;
        }
        resIdxById[rc.id] = static_cast<int>(i);
    }

    // Tasks. RM maps base priorities from period ranks BEFORE registration
    // (scheduler.hpp contract): rank by period ASC (ties keep declaration
    // order), periodic base = MRT_PRIO_MAX - rank, others MRT_PRIO_MIN.
    // FIFO and RR dispatch purely by enqueue order, but the kernel ready
    // queue is ALWAYS priority-ordered — equal base priorities (0) make
    // queue order = FIFO order by construction (aging may still boost).
    std::vector<std::int32_t> base(cfg_.tasks.size(), 0);
    if (st == SchedType::RM) {
        std::vector<std::size_t> order(cfg_.tasks.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [this](std::size_t a, std::size_t b) {
                             const std::int64_t pa =
                                 cfg_.tasks[a].period > 0
                                     ? cfg_.tasks[a].period
                                     : (std::int64_t)MRT_TIME_MAX;
                             const std::int64_t pb =
                                 cfg_.tasks[b].period > 0
                                     ? cfg_.tasks[b].period
                                     : (std::int64_t)MRT_TIME_MAX;
                             return pa < pb;
                         });
        std::int32_t rank = 0;
        for (const std::size_t i : order) {
            if (cfg_.tasks[i].period > 0) {
                base[i] = static_cast<std::int32_t>(MRT_PRIO_MAX) - rank;
                ++rank;
            } else {
                base[i] = MRT_PRIO_MIN;
            }
        }
    } else if (st == SchedType::FIFO || st == SchedType::RR) {
        for (std::size_t i = 0; i < cfg_.tasks.size(); ++i) {
            base[i] = 0; // pure queue order (see comment above)
        }
    } else {
        for (std::size_t i = 0; i < cfg_.tasks.size(); ++i) {
            base[i] = cfg_.tasks[i].priority;
        }
    }

    kidToIdx_.assign(MRT_MAX_TASKS, -1);
    rt_.resize(cfg_.tasks.size());
    for (std::size_t i = 0; i < cfg_.tasks.size(); ++i) {
        const TaskCfg& tc = cfg_.tasks[i];
        TaskRt& rt = rt_[i];
        rt.cfgIndex = static_cast<int>(i);
        const mrt_task_kind_t kind =
            tc.kind == TaskKind::Aperiodic
                ? MRT_TASK_APERIODIC
                : (tc.kind == TaskKind::Periodic ? MRT_TASK_PERIODIC
                                                 : MRT_TASK_SPORADIC);
        mrt_task_id_t kid = MRT_TASK_ID_NONE;
        (void)mrt_task_register(
            &k_, tc.id.c_str(), kind, base[i],
            static_cast<mrt_time_t>(tc.arrival),
            static_cast<mrt_time_t>(tc.period),
            static_cast<mrt_time_t>(tc.relativeDeadline), &kid);
        rt.kid = kid;
        if (kid != MRT_TASK_ID_NONE) {
            kidToIdx_[kid] = static_cast<int>(i);
            mrt_task_get(&k_, kid)->engine_ctx = i; // informational only
        }
        rt.stepResIdx.reserve(tc.steps.size());
        for (const StepCfg& s : tc.steps) {
            const auto it = resIdxById.find(s.res);
            rt.stepResIdx.push_back(
                (s.op == StepOp::LOCK || s.op == StepOp::UNLOCK ||
                 s.op == StepOp::WAIT || s.op == StepOp::SIGNAL ||
                 s.op == StepOp::SEND || s.op == StepOp::RECV ||
                 s.op == StepOp::EVWAIT || s.op == StepOp::EVSET) &&
                        it != resIdxById.end()
                    ? it->second
                    : -1);
        }
    }

    open_.assign(cfg_.tasks.size(), OpenSeg{});
    computeReleaseSchedules();

    record(0, trace_event::SIM_START, -1, -1, nullptr, nullptr, -1,
           json{{"duration", cfg_.duration},
                {"scheduler", schedTypeName(cfg_.scheduler.type)}});

    for (std::size_t i = 0; i < rt_.size(); ++i) {
        pushNextRelease(static_cast<int>(i));
    }
}

void Simulation::computeReleaseSchedules() {
    const mrt_time_t dur = static_cast<mrt_time_t>(cfg_.duration);
    for (std::size_t i = 0; i < cfg_.tasks.size(); ++i) {
        const TaskCfg& tc = cfg_.tasks[i];
        TaskRt& rt = rt_[i];
        if (tc.kind == TaskKind::Aperiodic) {
            rt.releases.push_back(static_cast<mrt_time_t>(tc.arrival));
        } else if (tc.kind == TaskKind::Periodic) {
            // Jitter draws: task declaration order, then job order — the
            // ONLY rng consumption in the engine. Releases are armed for
            // ticks STRICTLY INSIDE the horizon (a release at t == duration
            // could never execute — counting it would fake unfinished work).
            mrt_time_t at = static_cast<mrt_time_t>(tc.arrival);
            while (at < dur) {
                const mrt_time_t j =
                    tc.jitter > 0
                        ? static_cast<mrt_time_t>(
                              rng_.bounded(static_cast<std::uint64_t>(
                                               tc.jitter) +
                                           1u))
                        : 0u;
                if (at + j < dur) {
                    rt.releases.push_back(at + j);
                }
                at += static_cast<mrt_time_t>(tc.period);
            }
        } else { // sporadic
            for (const std::int64_t r : tc.releases) {
                rt.releases.push_back(static_cast<mrt_time_t>(r));
            }
        }
    }
}

// ---------------------------------------------------------------------
// event loop phases
// ---------------------------------------------------------------------

bool Simulation::eventStale(const SimEvent& e) const {
    if (e.kind == SimEvent::CPU_END || e.kind == SimEvent::QUANTUM) {
        const TaskRt& rt = rt_[static_cast<std::size_t>(e.taskIdx)];
        return rt.sliceEpoch != e.epoch;
    }
    return false;
}

bool Simulation::advanceTime() {
    // Drop invalidated events from the head so the reported end tick and
    // simulatedUntil stay tight (stale events never advance time).
    while (!events_.empty() && eventStale(events_.top())) {
        events_.pop();
    }
    mrt_time_t nextT = MRT_TIME_MAX;
    if (!events_.empty()) {
        nextT = events_.top().t;
    }
    const mrt_time_t tm = mrt_timer_next(&k_.timers);
    if (tm < nextT) {
        nextT = tm;
    }
    if (nextT == MRT_TIME_MAX) {
        // Natural end: queue empty, no timers, cpu idle by construction.
        // Quiesced-but-unfinished tasks (a deadlock, an orphaned consumer
        // blocked forever) hold their states until the horizon: their gantt
        // segments and accounting extend to `duration` (schema: tasks in a
        // deadlock remain BLOCKED to the end). A fully finished run keeps
        // its tight end tick.
        bool unfinished = false;
        for (const TaskRt& rt : rt_) {
            if (rt.jobNumber >= 0) {
                unfinished = true;
                break;
            }
        }
        endTick_ = unfinished
                       ? static_cast<mrt_time_t>(cfg_.duration)
                       : now_;
        if (unfinished && now_ < endTick_) {
            now_ = endTick_; // idle accounting covers the dead time
            k_.now = now_;
        }
        return false;
    }
    if (nextT > static_cast<mrt_time_t>(cfg_.duration)) {
        endedByDuration_ = true;
        endTick_ = static_cast<mrt_time_t>(cfg_.duration);
        return false;
    }
    if (trace_.full()) {
        endedByTraceCap_ = true;
        endTick_ = now_;
        return false;
    }
    now_ = nextT;
    k_.now = now_;
    return true;
}

void Simulation::processDueTimers() {
    mrt_timer_t tm;
    while (mrt_timer_pop_expired(&k_.timers, now_, &tm) == MRT_OK) {
        switch (tm.tag) {
        case MRT_TIMER_TAG_IO: {
            const int idx = kidToIdx_[tm.task];
            if (idx < 0) break;
            TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
            const StepCfg& st =
                cfg_.tasks[static_cast<std::size_t>(idx)]
                    .steps[rt.stepCursor];
            engineSetState(idx, MRT_TASK_READY, now_);
            readyPush(idx, now_);
            rt.stepResume = true; // the io step completed at wake
            record(now_, trace_event::IO_END, idx, -1,
                   trace_state::WAITING, trace_state::READY, -1,
                   json{{"dev", st.dev.empty() ? json() : json(st.dev)},
                        {"d", st.d}});
            break;
        }
        case MRT_TIMER_TAG_WAKE: {
            const int idx = kidToIdx_[tm.task];
            if (idx < 0) break;
            TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
            const StepCfg& st =
                cfg_.tasks[static_cast<std::size_t>(idx)]
                    .steps[rt.stepCursor];
            engineSetState(idx, MRT_TASK_READY, now_);
            readyPush(idx, now_);
            rt.stepResume = true; // the sleep step completed at wake
            record(now_, trace_event::SLEEP_END, idx, -1,
                   trace_state::SLEEPING, trace_state::READY, -1,
                   json{{"d", st.d}});
            break;
        }
        case MRT_TIMER_TAG_DEADLINE: {
            PendingDeadline p;
            p.taskIdx = kidToIdx_[tm.task];
            if (p.taskIdx < 0) break;
            p.jobNumber = rt_[static_cast<std::size_t>(p.taskIdx)].jobNumber;
            p.deadline = tm.fire_at;
            pendingDl_.push_back(p);
            break;
        }
        case MRT_TIMER_TAG_AGING: {
            agingTick(now_);
            if (k_.aging_interval > 0 &&
                now_ + k_.aging_interval <=
                    static_cast<mrt_time_t>(cfg_.duration)) {
                std::uint32_t h = 0;
                (void)mrt_timer_add(&k_.timers, now_ + k_.aging_interval,
                                    MRT_TASK_ID_NONE, MRT_TIMER_TAG_AGING,
                                    &h);
            }
            break;
        }
        default:
            break; // unknown tags ignored (future-proof)
        }
    }
}

void Simulation::processEventsAt(mrt_time_t t) {
    while (!events_.empty() && events_.top().t == t) {
        const SimEvent e = events_.top();
        events_.pop();
        if (eventStale(e)) {
            continue;
        }
        switch (e.kind) {
        case SimEvent::ARRIVAL:
            onArrival(e.taskIdx, t);
            break;
        case SimEvent::CTX_DONE:
            onCtxDone(e.taskIdx, t);
            break;
        case SimEvent::CPU_END:
            onCpuEnd(e.taskIdx, t);
            break;
        case SimEvent::QUANTUM:
            onQuantum(e.taskIdx, t);
            break;
        default:
            break;
        }
    }
}

void Simulation::finishTick(mrt_time_t t) {
    for (const PendingDeadline& p : pendingDl_) {
        TaskRt& rt = rt_[static_cast<std::size_t>(p.taskIdx)];
        if (rt.jobNumber != p.jobNumber) {
            continue; // completed exactly on time this tick (or replaced)
        }
        if (rt.deadlineMissed) {
            continue;
        }
        rt.deadlineMissed = true;
        mrt_task_get(&k_, rt.kid)->deadline_misses++;
        deadlineMisses_++;
        record(t, trace_event::DEADLINE_MISS, p.taskIdx, -1, nullptr, nullptr,
               -1,
               json{{"job", p.jobNumber},
                    {"deadline", p.deadline},
                    {"finished", false}});
        anomalies_.push_back(Anomaly{t, "deadlineMiss", p.taskIdx, -1,
                                     json{{"job", p.jobNumber},
                                          {"deadline", p.deadline},
                                          {"finished", false}}});
    }
}

// ---------------------------------------------------------------------
// event handlers
// ---------------------------------------------------------------------

void Simulation::onArrival(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const TaskCfg& tc = cfg_.tasks[static_cast<std::size_t>(idx)];
    if (rt.jobNumber >= 0) {
        // Deterministic policy: a release while a job is still active is
        // skipped (jobs never overlap) and reported as an anomaly.
        anomalies_.push_back(Anomaly{
            t, "skippedRelease", idx, -1,
            json{{"job", static_cast<std::int64_t>(rt.nextRelease)},
                 {"activeJob", rt.jobNumber}}});
        rt.nextRelease++;
        pushNextRelease(idx);
        return;
    }
    const int job = static_cast<int>(rt.nextRelease);
    rt.nextRelease++;
    rt.jobNumber = job;
    rt.releaseTick = t;
    rt.stepCursor = 0;
    rt.inCpuStep = false;
    rt.stepResume = false;
    rt.sliceEpoch++;
    rt.cpuRemaining = 0;
    rt.jobWait = 0;
    rt.firstDispatched = false;
    rt.deadlineMissed = false;
    rt.liveAllocs.clear();

    mrt_task_t* tcb = mrt_task_get(&k_, rt.kid);
    if (tcb->state == MRT_TASK_TERMINATED) {
        tcb->state = MRT_TASK_UNUSED; // job recycling (2-a convention)
    }
    engineSetState(idx, MRT_TASK_READY, t); // increments jobs_released
    readyPush(idx, t);
    record(t, job == 0 ? trace_event::TASK_ARRIVAL : trace_event::JOB_RELEASE,
           idx, -1, nullptr, trace_state::READY, -1, json{{"job", job}});
    if (tc.relativeDeadline > 0) {
        std::uint32_t h = 0;
        (void)mrt_timer_add(&k_.timers,
                            t + static_cast<mrt_time_t>(tc.relativeDeadline),
                            rt.kid, MRT_TIMER_TAG_DEADLINE, &h);
    }
    pushNextRelease(idx);
}

void Simulation::onCtxDone(int idx, mrt_time_t t) {
    switching_ = false;
    actualStart(idx, t);
}

void Simulation::onCpuEnd(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const TaskCfg& tc = cfg_.tasks[static_cast<std::size_t>(idx)];
    const std::int64_t ran =
        tc.steps[rt.stepCursor].d; // full logical burst length
    rt.inCpuStep = false;
    rt.sliceEpoch++; // the twin QUANTUM (same tick) is now stale
    rt.stepCursor++;
    record(t, trace_event::CPU_END, idx, -1, trace_state::RUNNING, nullptr,
           -1, json{{"ran", ran}});
    interpretSteps(idx, t);
}

void Simulation::onQuantum(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const std::int64_t ran =
        static_cast<std::int64_t>(t - rt.cpuStartTick);
    rt.cpuRemaining -= ran;
    if (rt.cpuRemaining < 0) {
        rt.cpuRemaining = 0;
    }
    rt.sliceEpoch++; // invalidate the CPU_END of this slice
    record(t, trace_event::QUANTUM_EXPIRE, idx, -1, trace_state::RUNNING,
           trace_state::READY, -1,
           json{{"quantum", sched_->quantum()}});
    engineSetState(idx, MRT_TASK_READY, t);
    readyPush(idx, t);
    cpuFreed(t, kReasonRotate);
}

// ---------------------------------------------------------------------
// scheduling
// ---------------------------------------------------------------------

SchedCandidate Simulation::candidateOf(int idx) const {
    const TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const TaskCfg& tc = cfg_.tasks[static_cast<std::size_t>(idx)];
    const mrt_task_t* tcb = mrt_task_get(const_cast<mrt_kernel_t*>(&k_),
                                         rt.kid);
    SchedCandidate c;
    c.id = rt.kid;
    c.effective = tcb->effective_priority;
    c.seq = tcb->enqueue_seq;
    c.remainingCpu = static_cast<std::uint64_t>(jobRemainingCpu(idx));
    c.absoluteDeadline =
        tc.relativeDeadline > 0
            ? rt.releaseTick + static_cast<mrt_time_t>(tc.relativeDeadline)
            : MRT_TIME_MAX;
    c.period = tc.kind == TaskKind::Periodic
                   ? static_cast<mrt_time_t>(tc.period)
                   : 0u;
    return c;
}

std::int64_t Simulation::jobRemainingCpu(int idx) const {
    const TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const std::vector<StepCfg>& steps =
        cfg_.tasks[static_cast<std::size_t>(idx)].steps;
    std::int64_t rem = rt.inCpuStep ? rt.cpuRemaining : 0;
    for (std::size_t i = rt.inCpuStep ? rt.stepCursor + 1 : rt.stepCursor;
         i < steps.size(); ++i) {
        if (steps[i].op == StepOp::CPU) {
            rem += steps[i].d;
        }
    }
    return rem;
}

int Simulation::bestCandidateIdx() const {
    if (mrt_readyq_size(const_cast<mrt_kernel_t*>(&k_)) == 0u) {
        return -1;
    }
    if (sched_->ranksByQueue()) {
        const mrt_task_id_t kid = mrt_readyq_peek(&k_);
        return kid == MRT_TASK_ID_NONE ? -1 : kidToIdx_[kid];
    }
    const std::uint32_t n = mrt_readyq_size(const_cast<mrt_kernel_t*>(&k_));
    int best = -1;
    SchedCandidate bc;
    for (std::uint32_t i = 0; i < n; ++i) {
        const mrt_task_id_t kid = mrt_readyq_at(&k_, i);
        if (kid == MRT_TASK_ID_NONE) continue;
        const int idx = kidToIdx_[kid];
        if (idx < 0) continue;
        const SchedCandidate c = candidateOf(idx);
        if (best < 0 || sched_->compare(c, bc) < 0) {
            best = idx;
            bc = c;
        }
    }
    return best;
}

void Simulation::scheduleTick(mrt_time_t t) {
    // (1) one preemption check per tick (only when a task is RUNNING).
    if (runningIdx_ >= 0 && sched_->preemptive()) {
        const int cand = bestCandidateIdx();
        if (cand >= 0) {
            const SchedCandidate cc = candidateOf(cand);
            const SchedCandidate rc = candidateOf(runningIdx_);
            if (sched_->compare(cc, rc) < 0) {
                preemptRunning(runningIdx_, t, cand);
            }
        }
    }
    // (2) dispatch loop: repeats while the cpu is free so zero-cpu
    // programs that finish instantly hand the cpu over in the same tick.
    while (cpuIdle_ &&
           mrt_readyq_size(const_cast<mrt_kernel_t*>(&k_)) > 0u) {
        const char* reason = freeReason_;
        const int w = readyPopIdx(t);
        if (w < 0) break;
        dispatchTask(w, t, reason);
        if (!cpuIdle_) break; // a burst or a context switch is in flight
    }
}

void Simulation::preemptRunning(int idx, mrt_time_t t, int byIdx) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const std::int64_t rem =
        rt.cpuRemaining - static_cast<std::int64_t>(t - rt.cpuStartTick);
    rt.cpuRemaining = rem > 0 ? rem : 0;
    rt.sliceEpoch++;
    record(t, trace_event::PREEMPT, idx, -1, trace_state::RUNNING,
           trace_state::READY, -1,
           json{{"by", taskId(byIdx)}, {"remaining", rt.cpuRemaining}});
    engineSetState(idx, MRT_TASK_READY, t);
    readyPush(idx, t);
    preemptions_++;
    mrt_kernel_note_preemption(&k_);
    cpuFreed(t, kReasonPreempt);
}

void Simulation::dispatchTask(int idx, mrt_time_t t, const char* reason) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    record(t, trace_event::DISPATCH, idx, -1, trace_state::READY,
           trace_state::RUNNING, -1, json{{"reason", reason}});
    schedNotes_.push_back(SchedNote{t, static_cast<std::int32_t>(idx),
                                    std::string(reason)});
    if (!rt.firstDispatched) {
        rt.firstDispatched = true;
        rt.responseTimes.push_back(
            static_cast<std::int64_t>(t - rt.releaseTick));
    }
    if (rt.firstStart == MRT_TIME_MAX) {
        rt.firstStart = t;
    }
    const int prev = lastRunningIdx_;
    if (prev != -1 && prev != idx && cfg_.contextSwitchCost > 0) {
        // CPU handover: `cost` ticks of unavailability, gantt "ctx" zone
        // attributed to the incoming task (schema example).
        record(t, trace_event::CONTEXT_SWITCH, idx, -1, nullptr, nullptr, -1,
               json{{"from", taskId(prev)},
                    {"to", taskId(idx)},
                    {"cost", cfg_.contextSwitchCost}});
        ctxSwitches_++;
        mrt_kernel_note_ctx_switch(&k_);
        mrt_kernel_note_ctx_overhead(
            &k_, static_cast<mrt_time_t>(cfg_.contextSwitchCost));
        ganttOpen(idx, trace_state::RUNNING, kCtxNote, t);
        busyFrom(t);
        switching_ = true;
        cpuIdle_ = false;
        rt.chosen = true;
        pushEvent(t + static_cast<mrt_time_t>(cfg_.contextSwitchCost), 2,
                  SimEvent::CTX_DONE, idx, 0);
        return; // the task starts at t + cost via CTX_DONE
    }
    actualStart(idx, t);
}

void Simulation::actualStart(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    busyFrom(t);
    switching_ = false;
    rt.chosen = false;
    runningIdx_ = idx;
    lastRunningIdx_ = idx;
    engineSetState(idx, MRT_TASK_RUNNING, t);
    if (rt.stepResume) {
        // A blocking step (io/sleep/lock/wait/evwait) completed at wake:
        // advance past it without re-executing. msgq steps are re-executed
        // instead (kernel contract), so they never set stepResume.
        rt.stepCursor++;
        rt.stepResume = false;
    }
    if (rt.inCpuStep) {
        startSlice(idx, t); // resume a partially-run cpu burst
    } else {
        interpretSteps(idx, t);
    }
}

void Simulation::startSlice(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    rt.cpuStartTick = t;
    rt.sliceEpoch++;
    record(t, trace_event::CPU_START, idx, -1, nullptr, nullptr, -1,
           json{{"remaining", rt.cpuRemaining}});
    // CPU_END first (lower seq) so it wins the same-tick race with QUANTUM.
    pushEvent(t + static_cast<mrt_time_t>(rt.cpuRemaining), 2,
              SimEvent::CPU_END, idx, rt.sliceEpoch);
    const std::uint32_t q = sched_->quantum();
    if (q > 0) {
        const std::uint64_t slice = static_cast<std::uint64_t>(q) <
                                            static_cast<std::uint64_t>(
                                                rt.cpuRemaining)
                                        ? static_cast<std::uint64_t>(q)
                                        : static_cast<std::uint64_t>(
                                              rt.cpuRemaining);
        pushEvent(t + static_cast<mrt_time_t>(slice), 2, SimEvent::QUANTUM,
                  idx, rt.sliceEpoch);
    }
}

// ---------------------------------------------------------------------
// program interpretation
// ---------------------------------------------------------------------

void Simulation::interpretSteps(int idx, mrt_time_t t) {
    const TaskCfg& tc = cfg_.tasks[static_cast<std::size_t>(idx)];
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    while (rt.stepCursor < tc.steps.size()) {
        const StepCfg& st = tc.steps[rt.stepCursor];
        const int ri = rt.stepResIdx[rt.stepCursor];
        switch (st.op) {
        case StepOp::CPU:
            rt.inCpuStep = true;
            rt.cpuRemaining = st.d;
            startSlice(idx, t);
            return;
        case StepOp::IO: {
            engineSetState(idx, MRT_TASK_WAITING, t);
            cpuFreed(t, kReasonIdle);
            std::uint32_t h = 0;
            (void)mrt_timer_add(&k_.timers,
                                t + static_cast<mrt_time_t>(st.d), rt.kid,
                                MRT_TIMER_TAG_IO, &h);
            record(t, trace_event::IO_START, idx, -1, trace_state::RUNNING,
                   trace_state::WAITING, st.d,
                   json{{"dev", st.dev.empty() ? json() : json(st.dev)},
                        {"d", st.d}});
            return;
        }
        case StepOp::SLEEP: {
            engineSetState(idx, MRT_TASK_SLEEPING, t);
            cpuFreed(t, kReasonIdle);
            std::uint32_t h = 0;
            (void)mrt_timer_add(&k_.timers,
                                t + static_cast<mrt_time_t>(st.d), rt.kid,
                                MRT_TIMER_TAG_WAKE, &h);
            record(t, trace_event::SLEEP_START, idx, -1,
                   trace_state::RUNNING, trace_state::SLEEPING, st.d,
                   json{{"d", st.d}});
            return;
        }
        case StepOp::LOCK: {
            const mrt_resource_t* res = mrt_res_get(&k_, ri);
            const mrt_task_id_t ownerBefore = res->owner;
            const std::vector<mrt_prio_t> eff = snapshotEff();
            int acquired = 0;
            const mrt_result_t rc = mrt_mutex_lock(&k_, ri, rt.kid, &acquired);
            diffEff(t, ri, eff);
            if (rc == MRT_OK && acquired) {
                record(t, trace_event::LOCK_ACQUIRE, idx, ri, nullptr,
                       nullptr, -1, json{{"res", resId(ri)}});
                rt.stepCursor++;
                continue;
            }
            const bool blockedNow =
                mrt_task_get(&k_, rt.kid)->state == MRT_TASK_BLOCKED;
            if (!blockedNow) {
                // Defensive: kernel refused without blocking (relock /
                // capacity edge). Skip the step deterministically.
                anomalies_.push_back(Anomaly{t, "lockInvalid", idx, ri,
                                             json{}});
                rt.stepCursor++;
                continue;
            }
            record(t, trace_event::LOCK_BLOCK, idx, ri, trace_state::RUNNING,
                   trace_state::BLOCKED, -1,
                   json{{"res", resId(ri)},
                        {"owner", ownerBefore == MRT_TASK_ID_NONE
                                      ? json()
                                      : json(taskId(
                                            kidToIdx_[ownerBefore]))}});
            ganttOpen(idx, trace_state::BLOCKED, nullptr, t);
            cpuFreed(t, kReasonIdle);
            detectDeadlocks(t); // basic RAG cycle detector (stage 3 refines)
            return;
        }
        case StepOp::UNLOCK: {
            const std::vector<mrt_prio_t> eff = snapshotEff();
            mrt_task_id_t handed = MRT_TASK_ID_NONE;
            const mrt_result_t rc = mrt_mutex_unlock(&k_, ri, rt.kid, &handed);
            diffEff(t, ri, eff);
            if (rc == MRT_OK) {
                record(t, trace_event::LOCK_RELEASE, idx, ri, nullptr, nullptr,
                       -1,
                       json{{"res", resId(ri)},
                            {"handedTo",
                             handed == MRT_TASK_ID_NONE
                                 ? json()
                                 : json(taskId(kidToIdx_[handed]))}});
                if (handed != MRT_TASK_ID_NONE) {
                    const int hidx = kidToIdx_[handed];
                    rt_[static_cast<std::size_t>(hidx)].stepResume = true;
                    record(t, trace_event::LOCK_ACQUIRE, hidx, ri, nullptr,
                           nullptr, -1,
                           json{{"res", resId(ri)},
                                {"handed", true}});
                }
                wakeScan(t);
                rt.stepCursor++;
                continue;
            }
            anomalies_.push_back(Anomaly{t, "unlockNotOwner", idx, ri,
                                         json{}});
            rt.stepCursor++;
            continue;
        }
        case StepOp::WAIT: {
            int acquired = 0;
            (void)mrt_sem_wait(&k_, ri, rt.kid, &acquired);
            record(t, trace_event::SEM_WAIT, idx, ri,
                   acquired ? nullptr : trace_state::RUNNING,
                   acquired ? nullptr : trace_state::BLOCKED, -1,
                   json{{"res", resId(ri)}, {"acquired", acquired != 0}});
            if (!acquired) {
                ganttOpen(idx, trace_state::BLOCKED, nullptr, t);
                cpuFreed(t, kReasonIdle);
                return;
            }
            rt.stepCursor++;
            continue;
        }
        case StepOp::SIGNAL: {
            mrt_task_id_t woken = MRT_TASK_ID_NONE;
            const mrt_result_t rc = mrt_sem_signal(&k_, ri, &woken);
            const bool overflow = rc == MRT_ERR_OVERFLOW;
            if (overflow) {
                anomalies_.push_back(
                    Anomaly{t, "semOverflow", -1, ri, json{}});
            }
            record(t, trace_event::SEM_SIGNAL, idx, ri, nullptr, nullptr, -1,
                   json{{"res", resId(ri)},
                        {"woken", woken == MRT_TASK_ID_NONE
                                      ? json()
                                      : json(taskId(kidToIdx_[woken]))},
                        {"overflow", overflow}});
            if (woken != MRT_TASK_ID_NONE) {
                rt_[static_cast<std::size_t>(kidToIdx_[woken])].stepResume =
                    true;
            }
            wakeScan(t);
            rt.stepCursor++;
            continue;
        }
        case StepOp::SEND: {
            int sent = 0;
            mrt_task_id_t woken = MRT_TASK_ID_NONE;
            (void)mrt_msgq_send(&k_, ri, rt.kid,
                                static_cast<std::int32_t>(st.msg), &sent,
                                &woken);
            record(t, trace_event::MSG_SEND, idx, ri,
                   sent ? nullptr : trace_state::RUNNING,
                   sent ? nullptr : trace_state::BLOCKED, -1,
                   json{{"res", resId(ri)},
                        {"msg", st.msg},
                        {"blocked", sent == 0}});
            if (sent) {
                wakeScan(t); // a receiver may have been woken (re-attempts)
                rt.stepCursor++;
                continue;
            }
            ganttOpen(idx, trace_state::BLOCKED, nullptr, t);
            cpuFreed(t, kReasonIdle);
            return; // the send re-executes on the next dispatch
        }
        case StepOp::RECV: {
            std::int32_t value = 0;
            int received = 0;
            (void)mrt_msgq_recv(&k_, ri, rt.kid, &value, &received);
            record(t, trace_event::MSG_RECV, idx, ri,
                   received ? nullptr : trace_state::RUNNING,
                   received ? nullptr : trace_state::BLOCKED, -1,
                   json{{"res", resId(ri)},
                        {"msg", received ? json(value) : json()},
                        {"blocked", received == 0}});
            if (received) {
                wakeScan(t); // a blocked sender may have been woken
                rt.stepCursor++;
                continue;
            }
            ganttOpen(idx, trace_state::BLOCKED, nullptr, t);
            cpuFreed(t, kReasonIdle);
            return; // the recv re-executes on the next dispatch
        }
        case StepOp::EVWAIT: {
            int satisfied = 0;
            (void)mrt_evflags_wait(&k_, ri, rt.kid, st.mask,
                                   st.modeAll ? 1 : 0, &satisfied);
            record(t, trace_event::EV_WAIT, idx, ri,
                   satisfied ? nullptr : trace_state::RUNNING,
                   satisfied ? nullptr : trace_state::BLOCKED, -1,
                   json{{"res", resId(ri)},
                        {"mask", st.mask},
                        {"mode", st.modeAll ? "all" : "any"},
                        {"satisfied", satisfied != 0}});
            if (!satisfied) {
                ganttOpen(idx, trace_state::BLOCKED, nullptr, t);
                cpuFreed(t, kReasonIdle);
                return;
            }
            rt.stepCursor++;
            continue;
        }
        case StepOp::EVSET: {
            mrt_task_id_t woken[MRT_MAX_WAITERS];
            std::uint32_t wcount = 0;
            (void)mrt_evflags_set(&k_, ri, st.mask, woken, MRT_MAX_WAITERS,
                                  &wcount);
            json wokenIds = json::array();
            for (std::uint32_t w = 0; w < wcount; ++w) {
                const int widx = kidToIdx_[woken[w]];
                wokenIds.push_back(taskId(widx));
                rt_[static_cast<std::size_t>(widx)].stepResume = true;
            }
            record(t, trace_event::EV_SET, idx, ri, nullptr, nullptr, -1,
                   json{{"res", resId(ri)}, {"mask", st.mask},
                        {"woken", wokenIds}});
            wakeScan(t);
            rt.stepCursor++;
            continue;
        }
        case StepOp::ALLOC:
            doAlloc(idx, t, st);
            rt.stepCursor++;
            continue;
        case StepOp::FREE:
            doFree(idx, t, st);
            rt.stepCursor++;
            continue;
        }
    }
    completeJob(idx, t);
}

void Simulation::completeJob(int idx, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const TaskCfg& tc = cfg_.tasks[static_cast<std::size_t>(idx)];
    // Deadline fallback (normally caught by the deferred timer verdict).
    if (tc.relativeDeadline > 0 &&
        t > rt.releaseTick + static_cast<mrt_time_t>(tc.relativeDeadline) &&
        !rt.deadlineMissed) {
        rt.deadlineMissed = true;
        mrt_task_get(&k_, rt.kid)->deadline_misses++;
        deadlineMisses_++;
        record(t, trace_event::DEADLINE_MISS, idx, -1, nullptr, nullptr, -1,
               json{{"job", rt.jobNumber},
                    {"deadline",
                     rt.releaseTick +
                         static_cast<mrt_time_t>(tc.relativeDeadline)},
                    {"finished", true}});
        anomalies_.push_back(Anomaly{
            t, "deadlineMiss", idx, -1,
            json{{"job", rt.jobNumber},
                 {"deadline",
                  rt.releaseTick +
                      static_cast<mrt_time_t>(tc.relativeDeadline)},
                 {"finished", true}}});
    }
    record(t, trace_event::TASK_COMPLETE, idx, -1, trace_state::RUNNING,
           trace_state::TERMINATED, -1, json{{"job", rt.jobNumber}});
    engineSetState(idx, MRT_TASK_TERMINATED, t);
    mrt_timer_cancel_all_for(&k_.timers, rt.kid);
    mrt_task_get(&k_, rt.kid)->jobs_completed++;
    completedJobs_++;
    rt.turnaroundTimes.push_back(
        static_cast<std::int64_t>(t - rt.releaseTick));
    rt.waitTimes.push_back(rt.jobWait);
    rt.lastFinish = t;
    for (const auto& la : rt.liveAllocs) {
        leaks_.push_back(LeakRec{static_cast<std::int32_t>(idx), la.first,
                                 la.second.second, la.second.first});
    }
    rt.liveAllocs.clear();
    rt.jobNumber = -1;
    rt.sliceEpoch++;
    cpuFreed(t, kReasonIdle);
}

// ---------------------------------------------------------------------
// memory model
// ---------------------------------------------------------------------

void Simulation::doAlloc(int idx, mrt_time_t t, const StepCfg& st) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    bool ok = false;
    std::int64_t off = -1;
    if (cfg_.memory.model == MemModel::Pool) {
        if (st.size <= cfg_.memory.blockSize) {
            std::size_t bi = 0;
            if (mrt_pool_alloc(&k_.pool, &bi) == MRT_OK) {
                ok = true;
                off = static_cast<std::int64_t>(bi);
            }
        } // size > blockSize: deterministic failure (documented)
    } else {
        std::size_t o = 0;
        if (mrt_mem_alloc(&k_.region, static_cast<std::size_t>(st.size),
                          &o) == MRT_OK) {
            ok = true;
            off = static_cast<std::int64_t>(o);
        }
    }
    record(t, trace_event::MEM_ALLOC, idx, -1, nullptr, nullptr, -1,
           json{{"size", st.size},
                {"tag", st.tag},
                {"offset", ok ? json(off) : json()},
                {"ok", ok}});
    addMemoryEvent(MemEventRec{t, true, static_cast<std::int32_t>(idx),
                               st.tag, st.size, ok, off});
    if (ok) {
        rt.liveAllocs[st.tag] = std::make_pair(off, st.size);
        allocOwner_[off] = std::make_pair(
            static_cast<std::int32_t>(idx),
            std::make_pair(st.tag, st.size));
    } else {
        anomalies_.push_back(Anomaly{t, "allocFailure", idx, -1,
                                     json{{"tag", st.tag},
                                          {"size", st.size}}});
    }
    sampleMemory(t);
}

void Simulation::doFree(int idx, mrt_time_t t, const StepCfg& st) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    const auto it = rt.liveAllocs.find(st.tag);
    if (it == rt.liveAllocs.end()) {
        // The matching alloc failed at runtime (or the tag was freed
        // already) — deterministic skip with an anomaly.
        anomalies_.push_back(
            Anomaly{t, "freeUnknownTag", idx, -1, json{{"tag", st.tag}}});
        return;
    }
    const std::int64_t off = it->second.first;
    const std::int64_t size = it->second.second;
    if (cfg_.memory.model == MemModel::Pool) {
        (void)mrt_pool_free(&k_.pool, static_cast<std::size_t>(off));
    } else {
        (void)mrt_mem_free(&k_.region, static_cast<std::size_t>(off));
    }
    record(t, trace_event::MEM_FREE, idx, -1, nullptr, nullptr, -1,
           json{{"tag", st.tag}, {"offset", off}, {"size", size}});
    addMemoryEvent(MemEventRec{t, false, static_cast<std::int32_t>(idx),
                               st.tag, size, true, off});
    rt.liveAllocs.erase(it);
    allocOwner_.erase(off);
    sampleMemory(t);
}

void Simulation::addMemoryEvent(const MemEventRec& ev) {
    memEvents_.push_back(ev);
    if (!ev.ok) {
        memFailures_.push_back(ev);
    }
}

void Simulation::sampleMemory(mrt_time_t t) {
    if (!cfg_.memory.enabled) {
        return;
    }
    std::int64_t used = 0;
    std::int64_t freeB = 0;
    std::int64_t largest = 0;
    std::int64_t frag = 0;
    if (cfg_.memory.model == MemModel::Pool) {
        const std::size_t blocks = mrt_pool_used(&k_.pool);
        used = static_cast<std::int64_t>(blocks) * cfg_.memory.blockSize;
        freeB = cfg_.memory.total - used;
        largest = static_cast<std::int64_t>(mrt_pool_free_count(&k_.pool)) *
                  cfg_.memory.blockSize;
        std::int64_t requested = 0;
        for (const auto& ao : allocOwner_) {
            requested += ao.second.second.second;
        }
        frag = used - requested; // internal fragmentation
    } else {
        used = static_cast<std::int64_t>(mrt_mem_used(&k_.region));
        freeB = static_cast<std::int64_t>(mrt_mem_free_total(&k_.region));
        largest =
            static_cast<std::int64_t>(mrt_mem_largest_free(&k_.region));
        frag = static_cast<std::int64_t>(mrt_mem_fragmentation(&k_.region));
    }
    memPeak_ = std::max(memPeak_, used);
    memUtilSum_ +=
        static_cast<double>(used) / static_cast<double>(cfg_.memory.total);
    memUtilSamples_++;
    fragSum_ += frag;
    fragMax_ = std::max(fragMax_, frag);
    fragSeries_.push_back(FragPoint{t, used, freeB, largest, frag});
}

// ---------------------------------------------------------------------
// aging / deadlock detection
// ---------------------------------------------------------------------

void Simulation::agingTick(mrt_time_t t) {
    if (!agingOn_) {
        return;
    }
    for (std::size_t i = 0; i < rt_.size(); ++i) {
        mrt_task_t* tcb = mrt_task_get(&k_, rt_[i].kid);
        if (tcb == nullptr || tcb->state != MRT_TASK_READY) {
            continue;
        }
        if (static_cast<std::int64_t>(tcb->aging_units) >= cfg_.aging.cap) {
            continue;
        }
        const mrt_prio_t before = tcb->effective_priority;
        (void)mrt_task_boost(&k_, rt_[i].kid, 1);
        if (tcb->effective_priority != before) {
            record(t, trace_event::AGING_BOOST, static_cast<int>(i), -1,
                   nullptr, nullptr, -1,
                   json{{"from", before}, {"to", tcb->effective_priority}});
        }
    }
}

void Simulation::detectDeadlocks(mrt_time_t t) {
    // Basic RAG cycle detection over mutex edges only (single-instance
    // resources): task -> waits-on -> mutex -> owned-by -> task ...
    const std::size_t n = rt_.size();
    for (std::size_t b = 0; b < n; ++b) {
        const mrt_task_id_t startKid = rt_[b].kid;
        std::vector<std::int32_t> cycle;
        std::vector<std::int32_t> cycTasks;
        std::vector<std::int32_t> cycRes;
        std::vector<char> visited(n, 0);
        mrt_task_id_t cur = startKid;
        bool found = false;
        while (true) {
            // find the mutex `cur` is blocked on
            int ri = -1;
            for (std::uint32_t r = 0; r < k_.resource_count; ++r) {
                const mrt_resource_t* res = &k_.resources[r];
                if (res->kind != MRT_RES_MUTEX) continue;
                for (std::uint32_t w = 0; w < res->waiter_count; ++w) {
                    if (res->waiters[w] == cur) {
                        ri = static_cast<int>(r);
                        break;
                    }
                }
                if (ri >= 0) break;
            }
            if (ri < 0) break; // not blocked on a mutex: chain ends
            const mrt_task_id_t owner = k_.resources[ri].owner;
            if (owner == MRT_TASK_ID_NONE) break;
            cycle.push_back(kidToIdx_[cur]);
            cycle.push_back(ri);
            cycTasks.push_back(kidToIdx_[cur]);
            cycRes.push_back(ri);
            if (owner == startKid) {
                found = true;
                break;
            }
            const int oidx = kidToIdx_[owner];
            if (oidx < 0 || visited[static_cast<std::size_t>(oidx)]) {
                break; // leads into another chain; reported from its start
            }
            visited[static_cast<std::size_t>(oidx)] = 1;
            cur = owner;
        }
        if (!found) continue;
        // dedupe on the sorted node set
        std::vector<std::int32_t> tk = cycTasks;
        std::vector<std::int32_t> rk = cycRes;
        std::sort(tk.begin(), tk.end());
        std::sort(rk.begin(), rk.end());
        std::string key;
        for (const std::int32_t v : tk) key += std::to_string(v) + ",";
        key += "|";
        for (const std::int32_t v : rk) key += std::to_string(v) + ",";
        if (std::find(deadlockKeys_.begin(), deadlockKeys_.end(), key) !=
            deadlockKeys_.end()) {
            continue;
        }
        deadlockKeys_.push_back(key);
        deadlocks_.push_back(
            DeadlockRec{t, cycle, cycTasks, cycRes});
        json cycNames = json::array();
        for (std::size_t i = 0; i < cycle.size(); ++i) {
            if (i % 2 == 0) { // alternating task, resource, task, ...
                cycNames.push_back(
                    cfg_.tasks[static_cast<std::size_t>(cycle[i])].id);
            } else {
                cycNames.push_back(
                    cfg_.resources[static_cast<std::size_t>(cycle[i])].id);
            }
        }
        record(t, trace_event::DEADLOCK, -1, -1, nullptr, nullptr, -1,
               json{{"cycle", cycNames}});
        json taskNames = json::array();
        for (const std::int32_t v : cycTasks) {
            taskNames.push_back(
                cfg_.tasks[static_cast<std::size_t>(v)].id);
        }
        json resNames = json::array();
        for (const std::int32_t v : cycRes) {
            resNames.push_back(
                cfg_.resources[static_cast<std::size_t>(v)].id);
        }
        anomalies_.push_back(Anomaly{t, "deadlock", -1, -1,
                                     json{{"tasks", taskNames},
                                          {"resources", resNames},
                                          {"detail", "circular wait"}}});
    }
}

// ---------------------------------------------------------------------
// bookkeeping helpers
// ---------------------------------------------------------------------

void Simulation::pushNextRelease(int idx) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    if (rt.nextRelease >= rt.releases.size()) {
        return;
    }
    const mrt_time_t at = rt.releases[rt.nextRelease];
    if (at > static_cast<mrt_time_t>(cfg_.duration)) {
        return; // beyond the horizon: never simulated
    }
    pushEvent(at, 1, SimEvent::ARRIVAL, idx, 0);
}

void Simulation::pushEvent(mrt_time_t t, int tieClass, int kind, int taskIdx,
                           std::uint64_t epoch) {
    SimEvent e;
    e.t = t;
    e.tieClass = tieClass;
    e.kind = kind;
    e.taskIdx = taskIdx;
    e.epoch = epoch;
    // Arrivals tie-break by task DECLARATION order (schema rule 3b);
    // completions by insertion order (rule 3c).
    e.seq = kind == SimEvent::ARRIVAL
                ? static_cast<std::uint64_t>(taskIdx)
                : evSeq_++;
    events_.push(std::move(e));
}

const char* Simulation::stateName(mrt_task_state_t st) {
    switch (st) {
    case MRT_TASK_READY: return trace_state::READY;
    case MRT_TASK_RUNNING: return trace_state::RUNNING;
    case MRT_TASK_BLOCKED: return trace_state::BLOCKED;
    case MRT_TASK_WAITING: return trace_state::WAITING;
    case MRT_TASK_SLEEPING: return trace_state::SLEEPING;
    case MRT_TASK_TERMINATED: return trace_state::TERMINATED;
    default: return nullptr;
    }
}

void Simulation::engineSetState(int idx, mrt_task_state_t st, mrt_time_t t) {
    TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
    (void)mrt_task_set_state(&k_, rt.kid, st, t);
    ganttOpen(idx, stateName(st), nullptr, t);
}

void Simulation::ganttOpen(int idx, const char* state, const char* note,
                           mrt_time_t t) {
    OpenSeg& os = open_[static_cast<std::size_t>(idx)];
    if (os.open && sameStr(os.state, state) && sameStr(os.note, note)) {
        return; // no visual change
    }
    if (os.open) {
        closeSegment(idx, t);
    }
    os.open = true;
    os.t0 = t;
    os.state = state;
    os.note = note;
}

void Simulation::closeSegment(int idx, mrt_time_t t) {
    OpenSeg& os = open_[static_cast<std::size_t>(idx)];
    if (os.open && t > os.t0) {
        gantt_.push_back(GanttSeg{static_cast<std::int32_t>(idx), os.t0, t,
                                  os.state, os.note});
        const std::int64_t len =
            static_cast<std::int64_t>(t - os.t0);
        TaskRt& rt = rt_[static_cast<std::size_t>(idx)];
        if (os.state == trace_state::RUNNING) {
            busyRunTime_ += len;
            if (os.note == nullptr) {
                rt.cpuTime += len;
            }
        } else if (os.state == trace_state::READY) {
            rt.readyWaitTotal += len;
            rt.jobWait += len;
        } else if (os.state == trace_state::BLOCKED) {
            rt.blockedTotal += len;
        } else if (os.state == trace_state::WAITING) {
            rt.waitingTotal += len;
        } else if (os.state == trace_state::SLEEPING) {
            rt.sleepingTotal += len;
        }
    }
    os.open = false;
}

void Simulation::ganttCloseAll(mrt_time_t t) {
    for (std::size_t i = 0; i < open_.size(); ++i) {
        closeSegment(static_cast<int>(i), t);
    }
}

void Simulation::queueLenChange(mrt_time_t t) {
    qIntegral_ += qLen_ * static_cast<std::int64_t>(t - qLastChange_);
    qLastChange_ = t;
}

void Simulation::readyPush(int idx, mrt_time_t t) {
    queueLenChange(t);
    (void)mrt_readyq_push(&k_, rt_[static_cast<std::size_t>(idx)].kid);
    rt_[static_cast<std::size_t>(idx)].queued = true;
    qLen_++;
    if (qLen_ > qMax_) {
        qMax_ = qLen_;
    }
}

int Simulation::readyPopIdx(mrt_time_t t) {
    mrt_task_id_t kid = MRT_TASK_ID_NONE;
    if (sched_->ranksByQueue()) {
        kid = mrt_readyq_pop(&k_);
    } else {
        const int best = bestCandidateIdx();
        if (best >= 0) {
            kid = rt_[static_cast<std::size_t>(best)].kid;
            (void)mrt_readyq_remove(&k_, kid);
        }
    }
    if (kid == MRT_TASK_ID_NONE) {
        return -1;
    }
    const int idx = kidToIdx_[kid];
    queueLenChange(t);
    rt_[static_cast<std::size_t>(idx)].queued = false;
    qLen_--;
    return idx;
}

void Simulation::wakeScan(mrt_time_t t) {
    // Kernel sync calls move woken tasks to READY without queueing them;
    // the engine pushes (declaration order) and mirrors the gantt state.
    for (std::size_t i = 0; i < rt_.size(); ++i) {
        TaskRt& rt = rt_[i];
        if (rt.chosen || static_cast<int>(i) == runningIdx_) {
            continue;
        }
        const mrt_task_t* tcb = mrt_task_get(&k_, rt.kid);
        if (tcb != nullptr && tcb->state == MRT_TASK_READY && !rt.queued) {
            ganttOpen(static_cast<int>(i), trace_state::READY, nullptr, t);
            readyPush(static_cast<int>(i), t);
        }
    }
}

std::vector<mrt_prio_t> Simulation::snapshotEff() const {
    std::vector<mrt_prio_t> v(rt_.size(), 0);
    for (std::size_t i = 0; i < rt_.size(); ++i) {
        v[i] = mrt_task_get(const_cast<mrt_kernel_t*>(&k_), rt_[i].kid)
                   ->effective_priority;
    }
    return v;
}

void Simulation::diffEff(mrt_time_t t, int resIdx,
                         const std::vector<mrt_prio_t>& before) {
    for (std::size_t i = 0; i < rt_.size(); ++i) {
        const mrt_prio_t cur =
            mrt_task_get(&k_, rt_[i].kid)->effective_priority;
        if (cur == before[i]) {
            continue;
        }
        record(t, cur > before[i] ? trace_event::LOCK_INHERIT
                                  : trace_event::LOCK_UNINHERIT,
               static_cast<int>(i), resIdx, nullptr, nullptr, -1,
               json{{"res", resId(resIdx)},
                    {"from", before[i]},
                    {"to", cur}});
    }
}

void Simulation::busyFrom(mrt_time_t t) {
    if (cpuIdle_) {
        idleTime_ += static_cast<std::int64_t>(t - idleStart_);
        mrt_kernel_note_idle(&k_, t - idleStart_);
        cpuIdle_ = false;
        busyStart_ = t;
    }
}

void Simulation::cpuFreed(mrt_time_t t, const char* reason) {
    if (!cpuIdle_) {
        mrt_kernel_note_cpu_time(&k_, t - busyStart_);
    }
    cpuIdle_ = true;
    switching_ = false;
    runningIdx_ = -1;
    idleStart_ = t;
    freeReason_ = reason;
}

bool Simulation::record(mrt_time_t t, const char* type, int taskIdx,
                        int resIdx, const char* from, const char* to,
                        std::int64_t dur, json detail) {
    return trace_.record(t, type, static_cast<std::int32_t>(taskIdx),
                         static_cast<std::int32_t>(resIdx), from, to, dur,
                         std::move(detail));
}

const char* Simulation::taskId(int idx) const {
    return idx >= 0 && static_cast<std::size_t>(idx) < cfg_.tasks.size()
               ? cfg_.tasks[static_cast<std::size_t>(idx)].id.c_str()
               : nullptr;
}

const char* Simulation::resId(int idx) const {
    return idx >= 0 && static_cast<std::size_t>(idx) < cfg_.resources.size()
               ? cfg_.resources[static_cast<std::size_t>(idx)].id.c_str()
               : nullptr;
}

// ---------------------------------------------------------------------
// finalization
// ---------------------------------------------------------------------

void Simulation::finalize() {
    if (cpuIdle_) {
        idleTime_ += static_cast<std::int64_t>(endTick_ - idleStart_);
        mrt_kernel_note_idle(&k_, endTick_ - idleStart_);
    }
    qIntegral_ +=
        qLen_ * static_cast<std::int64_t>(endTick_ - qLastChange_);
    ganttCloseAll(endTick_);
    std::stable_sort(
        gantt_.begin(), gantt_.end(),
        [](const GanttSeg& a, const GanttSeg& b) {
            if (a.t0 != b.t0) return a.t0 < b.t0;
            return a.taskIdx < b.taskIdx;
        });

    for (const TaskRt& rt : rt_) {
        releasedJobs_ += mrt_task_get(&k_, rt.kid)->jobs_released;
    }

    bool anyUnfinished = false;
    for (const TaskRt& rt : rt_) {
        if (rt.jobNumber >= 0) {
            anyUnfinished = true;
            break;
        }
    }
    const bool pending = !events_.empty() || k_.timers.count > 0;
    status_ = (anyUnfinished &&
               (endedByDuration_ || endedByTraceCap_ || pending))
                  ? "stopped"
                  : "completed";

    // memory final layout
    if (cfg_.memory.enabled) {
        if (cfg_.memory.model == MemModel::Region) {
            const mrt_mem_block_t* blocks = nullptr;
            const std::uint32_t n = mrt_mem_blocks(&k_.region, &blocks);
            for (std::uint32_t i = 0; i < n; ++i) {
                MemLayoutRec rec;
                rec.offset = static_cast<std::int64_t>(blocks[i].offset);
                rec.size = static_cast<std::int64_t>(blocks[i].size);
                if (blocks[i].used != 0) {
                    const auto it = allocOwner_.find(rec.offset);
                    if (it != allocOwner_.end()) {
                        rec.taskIdx = it->second.first;
                        rec.hasTag = true;
                        rec.tag = it->second.second.first;
                    }
                }
                memLayout_.push_back(rec);
            }
        } else {
            for (const auto& ao : allocOwner_) {
                MemLayoutRec rec;
                rec.offset = ao.first;
                rec.size = cfg_.memory.blockSize;
                rec.taskIdx = ao.second.first;
                rec.hasTag = true;
                rec.tag = ao.second.second.first;
                memLayout_.push_back(rec);
            }
        }
    }

    record(endTick_, trace_event::SIM_END, -1, -1, nullptr, nullptr, -1,
           json{{"status", status_},
                {"completedJobs", completedJobs_},
                {"releasedJobs", releasedJobs_},
                {"simulatedUntil", endTick_}});
    totalEvents_ = trace_.size();
}

} // namespace micrort
