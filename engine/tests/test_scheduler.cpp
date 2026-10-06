// test_scheduler.cpp — ordering / flag / quantum contracts of the 8
// dispatch policies, per SIMULATION_SCHEMA.md §Determinism & tie-breaking.
//
// For every scheduler: >= 3 candidate-ordering scenarios (including the
// documented tie rules), preemptive flag, ranksByQueue flag, quantum value
// and schedTypeName/schedTypeFromString round-trip.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#define TH_SUITE "test_scheduler"

#include "test_harness.hpp"

#include <micrort/scheduler.hpp>

#include <memory>
#include <string>
#include <vector>

using micrort::IScheduler;
using micrort::SchedulerCfg;
using micrort::SchedCandidate;
using micrort::SchedType;
using micrort::kSchedAperiodicPeriod;

namespace {

/// Candidate shorthand: (id, effective prio, enqueue seq, remaining CPU,
/// absolute deadline, period).
SchedCandidate cand(std::uint16_t id, std::int32_t eff, std::uint64_t seq,
                    std::uint64_t rem, std::uint64_t dl, std::uint64_t period) {
    SchedCandidate c;
    c.id = id;
    c.effective = eff;
    c.seq = seq;
    c.remainingCpu = rem;
    c.absoluteDeadline = dl;
    c.period = period;
    return c;
}

std::unique_ptr<IScheduler> make(SchedType t, std::int64_t quantum = 0) {
    SchedulerCfg cfg;
    cfg.type = t;
    cfg.quantum = quantum;
    return micrort::makeScheduler(cfg);
}

} // namespace

#define CHECK_BEFORE(s, a, b)                             \
    do {                                                  \
        CHECK_TRUE((s)->compare((a), (b)) < 0);           \
        CHECK_TRUE((s)->compare((b), (a)) > 0);           \
    } while (0)

int main() {
    // ------------------------------------------------------------------
    // Identity, flags and quantum for all 8 policies
    // ------------------------------------------------------------------
    TEST("sched names round-trip");
    {
        const std::vector<std::string> names = {
            "fifo", "rr", "priority", "priority_p", "sjf", "srtf", "rm", "edf"};
        for (const std::string& n : names) {
            SchedType t = SchedType::FIFO;
            CHECK_TRUE(micrort::schedTypeFromString(n, t));
            CHECK_STREQ(micrort::schedTypeName(t), n);
        }
        SchedType t = SchedType::FIFO;
        CHECK_TRUE(!micrort::schedTypeFromString("lottery", t));
        CHECK_TRUE(!micrort::schedTypeFromString("", t));
    }

    TEST("factory identity + flags matrix");
    {
        struct Row {
            SchedType type;
            const char* name;
            bool preemptive;
            bool ranksByQueue;
            std::uint32_t quantum;
        };
        const std::vector<Row> rows = {
            {SchedType::FIFO, "fifo", false, true, 0},
            {SchedType::RR, "rr", true, true, 4}, // default quantum
            {SchedType::Priority, "priority", false, true, 0},
            {SchedType::PriorityP, "priority_p", true, true, 0},
            {SchedType::SJF, "sjf", false, false, 0},
            {SchedType::SRTF, "srtf", true, false, 0},
            {SchedType::RM, "rm", true, false, 0},
            {SchedType::EDF, "edf", true, false, 0},
        };
        for (const Row& r : rows) {
            const auto s = make(r.type);
            CHECK_TRUE(s != nullptr);
            CHECK_TRUE(s->type() == r.type);
            CHECK_STREQ(micrort::schedTypeName(s->type()), r.name);
            CHECK_TRUE(s->preemptive() == r.preemptive);
            CHECK_TRUE(s->ranksByQueue() == r.ranksByQueue);
            CHECK_EQ(s->quantum(), r.quantum);
        }
    }

    TEST("rr quantum explicit and defensive fallback");
    {
        CHECK_EQ(make(SchedType::RR, 7)->quantum(), 7u);
        CHECK_EQ(make(SchedType::RR, 1)->quantum(), 1u);
        CHECK_EQ(make(SchedType::RR, 1000)->quantum(), 1000u);
        CHECK_EQ(make(SchedType::RR, 0)->quantum(), 4u);   // unvalidated cfg
        CHECK_EQ(make(SchedType::RR, 1001)->quantum(), 4u); // unvalidated cfg
        CHECK_EQ(make(SchedType::EDF, 9)->quantum(), 0u);   // non-rr: none
    }

    // ------------------------------------------------------------------
    // FIFO: pure queue order
    // ------------------------------------------------------------------
    TEST("fifo orders by enqueue seq only");
    {
        const auto s = make(SchedType::FIFO);
        CHECK_BEFORE(s, cand(1, 1, 10, 5, 100, 20),
                          cand(2, 9, 20, 1, 5, 10)); // seq beats priority
        CHECK_BEFORE(s, cand(5, 50, 3, 9, 99, 99),
                          cand(1, 1, 4, 1, 1, 1));   // earlier seq first
        CHECK_EQ(s->compare(cand(3, 7, 8, 2, 30, 40),
                            cand(3, 7, 8, 2, 30, 40)), 0); // identical
        CHECK_BEFORE(s, cand(2, 0, 7, 0, 0, 0),
                          cand(9, 0, 7, 0, 0, 0));    // id as last resort
    }

    // ------------------------------------------------------------------
    // RR: same ordering + quantum rotation
    // ------------------------------------------------------------------
    TEST("rr orders by enqueue seq (quantum adds rotation, not rank)");
    {
        const auto s = make(SchedType::RR, 4);
        CHECK_BEFORE(s, cand(1, 2, 100, 5, 50, 50),
                          cand(2, 8, 200, 1, 10, 10));
        CHECK_BEFORE(s, cand(7, -5, 1, 100, 100, 100),
                          cand(3, 5, 2, 1, 1, 1));
        CHECK_BEFORE(s, cand(1, 5, 42, 3, 30, 30),
                          cand(2, 5, 43, 3, 30, 30));
    }

    // ------------------------------------------------------------------
    // Priority / PriorityP: effective priority DESC, seq ASC
    // ------------------------------------------------------------------
    TEST("priority orders by effective priority then seq");
    {
        const auto s = make(SchedType::Priority);
        CHECK_BEFORE(s, cand(1, 5, 10, 9, 99, 99),
                          cand(2, 1, 20, 1, 1, 1));       // higher prio first
        CHECK_BEFORE(s, cand(1, 5, 10, 9, 99, 99),
                          cand(2, 5, 20, 1, 1, 1));       // tie -> earlier seq
        CHECK_BEFORE(s, cand(1, -1, 10, 9, 99, 99),
                          cand(2, -5, 20, 1, 1, 1));      // negatives ordered
        CHECK_TRUE(s->compare(cand(1, 4, 10, 9, 99, 99),
                              cand(2, 4, 10, 1, 1, 1)) != 0); // id final guard
    }
    TEST("priority_p same rank, preemption on");
    {
        const auto s = make(SchedType::PriorityP);
        CHECK_TRUE(s->preemptive());
        CHECK_BEFORE(s, cand(1, 100, 5, 50, 50, 50),
                          cand(2, 99, 1, 1, 1, 1));
        CHECK_BEFORE(s, cand(1, 0, 1, 50, 50, 50),
                          cand(2, -1, 2, 1, 1, 1));
        CHECK_BEFORE(s, cand(1, 7, 30, 0, 0, 0),
                          cand(2, 7, 31, 0, 0, 0));
    }

    // ------------------------------------------------------------------
    // SJF / SRTF: remaining CPU ASC, seq ASC; priority ignored
    // ------------------------------------------------------------------
    TEST("sjf orders by remaining CPU then seq");
    {
        const auto s = make(SchedType::SJF);
        CHECK_TRUE(!s->preemptive());
        CHECK_BEFORE(s, cand(1, 1, 10, 3, 99, 99),
                          cand(2, 9, 20, 5, 1, 1));       // shorter job first
        CHECK_BEFORE(s, cand(1, 1, 10, 4, 99, 99),
                          cand(2, 9, 20, 4, 1, 1));       // tie -> earlier seq
        CHECK_BEFORE(s, cand(1, 1, 10, 4, 99, 99),
                          cand(2, 100, 20, 5, 1, 1));     // beats raw priority
        CHECK_EQ(s->compare(cand(4, 0, 9, 7, 0, 0),
                            cand(4, 0, 9, 7, 0, 0)), 0); // identical
    }
    TEST("srtf same rank, preemption on");
    {
        const auto s = make(SchedType::SRTF);
        CHECK_TRUE(s->preemptive());
        CHECK_BEFORE(s, cand(1, 0, 10, 1, 0, 0),
                          cand(2, 0, 11, 2, 0, 0));
        CHECK_BEFORE(s, cand(1, -100, 50, 10, 0, 0),
                          cand(2, 100, 51, 11, 0, 0));
        CHECK_BEFORE(s, cand(3, 0, 12, 6, 0, 0),
                          cand(4, 0, 13, 6, 0, 0)); // tie -> seq
    }

    // ------------------------------------------------------------------
    // RM: period ASC; aperiodic (MRT_TIME_MAX or raw 0) last; tie -> seq
    // ------------------------------------------------------------------
    TEST("rm orders by period then seq");
    {
        const auto s = make(SchedType::RM);
        CHECK_TRUE(s->preemptive());
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 99, 10),
                          cand(2, 9, 20, 1, 1, 20));      // shorter period
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 99, 20),
                          cand(2, 9, 20, 1, 1, kSchedAperiodicPeriod));
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 99, 1),
                          cand(2, 9, 20, 1, 1, 0));       // raw 0 = aperiodic
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 99, 30),
                          cand(2, 9, 20, 1, 1, 30));      // tie -> earlier seq
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 99, kSchedAperiodicPeriod),
                          cand(2, 9, 20, 1, 1, kSchedAperiodicPeriod)); // both aperiodic
    }

    // ------------------------------------------------------------------
    // EDF: absolute deadline ASC; MRT_TIME_MAX last; tie -> seq
    // ------------------------------------------------------------------
    TEST("edf orders by absolute deadline then seq");
    {
        const auto s = make(SchedType::EDF);
        CHECK_TRUE(s->preemptive());
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 10, 99),
                          cand(2, 9, 20, 1, 20, 1));      // earlier deadline
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 100, 7),
                          cand(2, 9, 20, 1, MRT_TIME_MAX, 1)); // none last
        CHECK_BEFORE(s, cand(1, 0, 10, 9, MRT_TIME_MAX, 99),
                          cand(2, 9, 20, 1, MRT_TIME_MAX, 1)); // tie -> seq
        CHECK_BEFORE(s, cand(1, 0, 10, 9, 25, 99),
                          cand(2, 9, 20, 1, 25, 1));      // tie -> earlier seq
        CHECK_EQ(s->compare(cand(6, 0, 9, 0, 5, 0),
                            cand(6, 0, 9, 0, 5, 0)), 0);  // identical
    }

    // ------------------------------------------------------------------
    // Cross-check: queue-ranked schedulers mirror kernel queue order
    // ------------------------------------------------------------------
    TEST("queue-ranked compare mirrors (effective DESC, seq ASC)");
    {
        // The kernel ready queue ranks (effective DESC, seq ASC); the
        // compare() of priority/priority_p must agree with that order.
        const SchedCandidate hi = cand(1, 10, 5, 0, 0, 0);
        const SchedCandidate lo = cand(2, 3, 2, 0, 0, 0); // weaker, earlier seq
        for (const SchedType t : {SchedType::Priority, SchedType::PriorityP}) {
            const auto s = make(t);
            CHECK_BEFORE(s, hi, lo);
        }
        // fifo/rr ignore priority: lo (earlier seq) first even though weaker.
        for (const SchedType t : {SchedType::FIFO, SchedType::RR}) {
            const auto s = make(t);
            CHECK_BEFORE(s, lo, hi);
        }
    }

    return TEST_SUMMARY();
}
