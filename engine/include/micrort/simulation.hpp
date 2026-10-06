// simulation.hpp — MicroRT-Lab discrete-event simulation core (stage 2-b2).
//
// The Simulation class owns ONE mrt_kernel_t (the C kernel), the engine
// task runtimes, the event queue, the scheduler policy, the trace
// recorder, the gantt segment builder and every metrics accumulator. RAII,
// no globals, no unordered containers — output paths iterate declaration
// order or ordered maps only, so the result document is byte-identical for
// identical (config, seed) input.
//
// Event loop shape (SIMULATION_SCHEMA.md §Determinism & tie-breaking):
//   1. t = min(engine event queue head, kernel timer list head);
//      stop naturally when both are exhausted (cpu is then idle by
//      construction), or at the duration horizon, or when the trace cap
//      is hit.
//   2. advance k_.now = t; process ALL due kernel timers in list order
//      (IO_END / SLEEP_END / deadline markers / aging ticks).
//   3. process engine events at t in (tieClass, seq) order:
//        class 1: ARRIVAL / job releases
//        class 2: completions (CTX_DONE, CPU_END, QUANTUM)
//   4. exactly one preemption check + a dispatch loop (repeats while the
//      cpu is free and the ready queue is non-empty, so zero-cpu programs
//      that complete instantly keep the same-tick handover deterministic).
//   5. deferred deadline-miss verdicts (a job completing exactly AT its
//      deadline tick is ON TIME — the verdict runs after step 4).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_SIMULATION_HPP
#define MICRORT_SIMULATION_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <micrort/config.hpp>
#include <micrort/rng.hpp>
#include <micrort/scheduler.hpp>
#include <micrort/trace.hpp>
#include <micrort/mrt_kernel.h>

namespace micrort {

class Simulation;                       // below
nlohmann::json buildResultDoc(const Simulation& sim,
                              std::uint64_t wallMicros); // result.hpp/.cpp

/// Engine-side per-task runtime state (mirrors the 2-b2 design notes).
/// The kernel TCB carries global statistics; TaskRt carries the current
/// job's interpretive state.
struct TaskRt {
    // config echo
    int cfgIndex = -1;                 ///< index into Config::tasks
    mrt_task_id_t kid = MRT_TASK_ID_NONE; ///< kernel handle
    std::vector<std::int32_t> stepResIdx; ///< res index per step (-1: none)

    // release schedule (precomputed at init, ascending)
    std::vector<mrt_time_t> releases;
    std::size_t nextRelease = 0;       ///< next un-simulated release index

    // current job (-1 = no active job)
    int jobNumber = -1;
    mrt_time_t releaseTick = 0;
    std::size_t stepCursor = 0;        ///< index of the step in flight / next
    std::int64_t cpuRemaining = 0;     ///< ticks left of the current cpu step
    bool inCpuStep = false;            ///< stepCursor is a partially-run cpu step
    bool stepResume = false;           ///< blocked step completed at wake; skip it
    mrt_time_t cpuStartTick = 0;       ///< when the current cpu slice began
    std::uint64_t sliceEpoch = 0;      ///< invalidates stale CPU_END/QUANTUM events

    // queue / cpu membership (engine-side mirror; kernel state is truth)
    bool queued = false;               ///< currently in the ready queue
    bool chosen = false;               ///< mid context switch into the cpu

    // per-job statistics
    mrt_time_t firstDispatchTick = MRT_TIME_MAX; ///< -1-ish sentinel (unset)
    bool firstDispatched = false;
    mrt_time_t readyEnterTick = 0;     ///< gantt READY-open (job wait accounting)
    std::int64_t jobWait = 0;          ///< READY residence of the current job
    bool deadlineMissed = false;

    // per-task job statistics (one entry per completed job; responseTimes
    // gains one entry per dispatched job)
    std::vector<std::int64_t> responseTimes;
    std::vector<std::int64_t> turnaroundTimes;
    std::vector<std::int64_t> waitTimes;

    // task-level aggregates (gantt segment closes accumulate here)
    std::int64_t cpuTime = 0;          ///< RUNNING segments without ctx note
    std::int64_t readyWaitTotal = 0;   ///< READY segments
    std::int64_t blockedTotal = 0;
    std::int64_t waitingTotal = 0;
    std::int64_t sleepingTotal = 0;
    mrt_time_t firstStart = MRT_TIME_MAX; ///< first DISPATCH tick (unset = MAX)
    mrt_time_t lastFinish = MRT_TIME_MAX; ///< last TASK_COMPLETE tick

    /// live allocations of the CURRENT job: tag -> {offsetOrBlock, size}
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> liveAllocs;
};

/// One gantt segment (per task, contiguous, zero-length segments dropped).
struct GanttSeg {
    int taskIdx = -1;
    mrt_time_t t0 = 0;
    mrt_time_t t1 = 0;
    const char* state = nullptr;       ///< RUNNING/READY/BLOCKED/WAITING/SLEEPING/TERMINATED
    const char* note = nullptr;        ///< "ctx" for context-switch zones, else null
};

/// Engine event (arrivals + completions). Timers live in the kernel list.
struct SimEvent {
    enum Kind { ARRIVAL = 1, CTX_DONE = 2, CPU_END = 3, QUANTUM = 4 };
    mrt_time_t t = 0;
    int tieClass = 0;                  ///< 1 arrivals, 2 completions (schema rule 3)
    std::uint64_t seq = 0;             ///< insertion order (final tie-break)
    int kind = ARRIVAL;
    int taskIdx = -1;
    std::uint64_t epoch = 0;           ///< sliceEpoch stamp for CPU_END/QUANTUM
};

/// Anomaly entry (schema §result document "anomalies").
struct Anomaly {
    mrt_time_t t = 0;
    std::string type;                  ///< deadlineMiss|semOverflow|allocFailure|skippedRelease|deadlock|freeUnknownTag|unlockNotOwner
    std::int32_t taskIdx = -1;
    std::int32_t resIdx = -1;
    nlohmann::json detail;
};

/// Deadlock record (schema "deadlocks[]").
struct DeadlockRec {
    mrt_time_t t = 0;
    std::vector<std::int32_t> cycle;   ///< alternating taskIdx / resIdx, starting+ending at a task
    std::vector<std::int32_t> tasks;
    std::vector<std::int32_t> resources;
};

/// One DISPATCH note (schema "schedulerNotes[]").
struct SchedNote {
    mrt_time_t t = 0;
    std::int32_t taskIdx = -1;
    std::string reason;                ///< "idle" | "preempt" | "rotate"
};

/// Memory event / layout entry (schema "memory").
struct MemEventRec {
    mrt_time_t t = 0;
    bool alloc = true;
    std::int32_t taskIdx = -1;
    std::string tag;
    std::int64_t size = 0;
    bool ok = true;
    std::int64_t offset = -1;          ///< region offset or pool block index
};
struct MemLayoutRec {
    std::int64_t offset = 0;
    std::int64_t size = 0;
    std::int32_t taskIdx = -1;         ///< -1 = free space
    bool hasTag = false;
    std::string tag;
};
struct FragPoint {
    mrt_time_t t = 0;
    std::int64_t used = 0;
    std::int64_t free = 0;
    std::int64_t largest = 0;
    std::int64_t frag = 0;
};

/// Simulation — one run, one kernel. Construct with the parsed+validated
/// Config and the ORIGINAL parsed config DOM (echoed verbatim into the
/// result document); run() returns the complete result document.
class Simulation {
public:
    Simulation(const Config& cfg, const nlohmann::json& originalConfig);
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    ~Simulation();

    /// Execute the event loop and return the full result document
    /// (schema micrort-result/1). Never throws.
    nlohmann::json run();

    friend nlohmann::json buildResultDoc(const Simulation& sim,
                                         std::uint64_t wallMicros);

private:
    // ---- setup ----
    void initKernel();
    void computeReleaseSchedules();

    // ---- event loop phases ----
    bool advanceTime();                       ///< returns false when the run ends
    void processDueTimers();
    void processEventsAt(mrt_time_t t);
    void scheduleTick(mrt_time_t t);
    void finishTick(mrt_time_t t);            ///< deferred deadline verdicts

    // ---- event handlers ----
    void onArrival(int idx, mrt_time_t t);
    void onCtxDone(int idx, mrt_time_t t);
    void onCpuEnd(int idx, mrt_time_t t);
    void onQuantum(int idx, mrt_time_t t);

    // ---- scheduling ----
    void preemptRunning(int idx, mrt_time_t t, int byIdx);
    void dispatchLoop(mrt_time_t t);
    void dispatchTask(int idx, mrt_time_t t, const char* reason);
    void actualStart(int idx, mrt_time_t t);
    void startSlice(int idx, mrt_time_t t);
    SchedCandidate candidateOf(int idx) const;
    int bestCandidateIdx() const;             ///< -1 when queue empty

    // ---- program interpretation ----
    void interpretSteps(int idx, mrt_time_t t);
    void completeJob(int idx, mrt_time_t t);
    void doAlloc(int idx, mrt_time_t t, const StepCfg& step);
    void doFree(int idx, mrt_time_t t, const StepCfg& step);

    // ---- helpers ----
    void finalize();
    void pushNextRelease(int idx);
    void pushEvent(mrt_time_t t, int tieClass, int kind, int taskIdx,
                   std::uint64_t epoch);
    void engineSetState(int idx, mrt_task_state_t st, mrt_time_t t);
    void ganttOpen(int idx, const char* state, const char* note, mrt_time_t t);
    void closeSegment(int idx, mrt_time_t t);
    void ganttCloseAll(mrt_time_t t);
    void readyPush(int idx, mrt_time_t t);
    int readyPopIdx(mrt_time_t t);
    void wakeScan(mrt_time_t t);
    void agingTick(mrt_time_t t);
    void detectDeadlocks(mrt_time_t t);
    void busyFrom(mrt_time_t t);
    void cpuFreed(mrt_time_t t, const char* reason);
    bool eventStale(const SimEvent& e) const;
    std::vector<mrt_prio_t> snapshotEff() const;
    void diffEff(mrt_time_t t, int resIdx,
                 const std::vector<mrt_prio_t>& before);
    static const char* stateName(mrt_task_state_t st);
    bool record(mrt_time_t t, const char* type, int taskIdx, int resIdx,
                const char* from, const char* to, std::int64_t dur,
                nlohmann::json detail);
    void queueLenChange(mrt_time_t t);
    void addMemoryEvent(const MemEventRec& ev);
    void sampleMemory(mrt_time_t t);
    std::int64_t jobRemainingCpu(int idx) const;
    const char* taskId(int idx) const;
    const char* resId(int idx) const;

    // ---- state ----
    Config cfg_;                            ///< validated copy
    nlohmann::json originalConfig_;         ///< echo source
    std::unique_ptr<IScheduler> sched_;

    mrt_kernel_t k_{};                      ///< THE kernel instance
    std::vector<TaskRt> rt_;                ///< parallel to cfg_.tasks
    std::vector<int> kidToIdx_;             ///< kernel id -> engine index
    std::vector<std::vector<std::int32_t>> msgqBufs_; ///< engine-owned msgq backing
    Xoshiro256 rng_;                        ///< jitter only

    TraceRecorder trace_;

    // engine event queue: min-heap on (t, tieClass, seq)
    struct EvCmp {
        bool operator()(const SimEvent& a, const SimEvent& b) const {
            if (a.t != b.t) return a.t > b.t;
            if (a.tieClass != b.tieClass) return a.tieClass > b.tieClass;
            return a.seq > b.seq;
        }
    };
    std::priority_queue<SimEvent, std::vector<SimEvent>, EvCmp> events_;
    std::uint64_t evSeq_ = 0;

    // gantt
    std::vector<GanttSeg> gantt_;
    struct OpenSeg {
        bool open = false;
        mrt_time_t t0 = 0;
        const char* state = nullptr;
        const char* note = nullptr;
    };
    std::vector<OpenSeg> open_;

    // cpu
    int runningIdx_ = -1;                   ///< task executing right now
    int lastRunningIdx_ = -1;               ///< most recent cpu occupant (ctx "from")
    bool switching_ = false;                ///< ctx switch in flight
    bool cpuIdle_ = true;                   ///< neither running nor switching
    mrt_time_t idleStart_ = 0;
    std::int64_t idleTime_ = 0;
    std::int64_t busyRunTime_ = 0;          ///< RUNNING (incl ctx) segment time
    std::uint64_t ctxSwitches_ = 0;
    std::uint64_t preemptions_ = 0;
    const char* freeReason_ = "idle";       ///< why the cpu last became free

    // ready-queue length integral
    std::int64_t qLen_ = 0;
    std::int64_t qIntegral_ = 0;
    mrt_time_t qLastChange_ = 0;
    std::int64_t qMax_ = 0;

    // per-tick bookkeeping
    struct PendingDeadline {
        int taskIdx = -1;
        int jobNumber = -1;
        mrt_time_t deadline = 0;
    };
    std::vector<PendingDeadline> pendingDl_;

    // aggregates
    std::uint64_t totalEvents_ = 0;
    std::vector<Anomaly> anomalies_;
    std::vector<DeadlockRec> deadlocks_;
    std::vector<SchedNote> schedNotes_;
    std::vector<std::string> deadlockKeys_; ///< dedupe keys (sorted cycle nodes)

    // memory bookkeeping
    std::vector<MemEventRec> memEvents_;
    std::vector<MemLayoutRec> memLayout_;   ///< built at end from the live model
    std::vector<FragPoint> fragSeries_;
    std::map<std::int64_t, std::pair<std::int32_t, std::pair<std::string, std::int64_t>>>
        allocOwner_;                        ///< offset/block -> {taskIdx, {tag, size}}
    std::int64_t memPeak_ = 0;
    double memUtilSum_ = 0.0;             ///< Σ used/total over memory events
    std::int64_t memUtilSamples_ = 0;
    std::int64_t fragSum_ = 0;
    std::int64_t fragMax_ = 0;
    std::vector<MemEventRec> memFailures_;
    struct LeakRec {
        std::int32_t taskIdx;
        std::string tag;
        std::int64_t size;
        std::int64_t offset;
    };
    std::vector<LeakRec> leaks_;

    // run status
    bool ran_ = false;
    mrt_time_t now_ = 0;
    mrt_time_t endTick_ = 0;
    bool endedByDuration_ = false;
    bool endedByTraceCap_ = false;
    bool agingOn_ = false;                 ///< aging timer armed (priority*/rr)
    mrt_time_t busyStart_ = 0;             ///< cpu busy interval start (kernel notes)
    std::string status_;
    std::uint64_t completedJobs_ = 0;
    std::uint64_t releasedJobs_ = 0;
    std::uint64_t deadlineMisses_ = 0;
};

} // namespace micrort

#endif // MICRORT_SIMULATION_HPP
