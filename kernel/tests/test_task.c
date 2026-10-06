/* test_task.c — task table, state machine, statistics, priority helpers.

Copyright © 2026 Parsa Fathi. Apache-2.0. */
#define TH_SUITE "test_task"
#include "harness.h"
#include "micrort/mrt_kernel.h"

#include <string.h>

static mrt_kernel_t K;

static void reset(void)
{
    mrt_kernel_init(&K);
}

static mrt_task_id_t reg(const char *name, mrt_prio_t prio)
{
    mrt_task_id_t id = MRT_TASK_ID_NONE;
    TEST_ASSERT_EQ_INT(MRT_OK,
        mrt_task_register(&K, name, MRT_TASK_APERIODIC, prio, 0, 0, 0, &id));
    return id;
}

int main(void)
{
    /* ============ kernel init + counter helpers ============ */
    TEST_NAME("kernel-init");
    reset();
    TEST_ASSERT_EQ_INT(0, (int)mrt_kernel_task_count(&K));
    TEST_ASSERT_EQ_INT(0, (int)mrt_kernel_resource_count(&K));
    TEST_ASSERT_EQ_U64(1, K.next_seq);          /* seq 0 = "never queued" */
    TEST_ASSERT_EQ_U64(0, K.cpu_busy_time);
    TEST_ASSERT_EQ_U64(0, K.idle_time);
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_readyq_peek(&K));
    mrt_kernel_init(NULL);                      /* must not crash */
    mrt_kernel_note_ctx_switch(NULL);
    mrt_kernel_note_preemption(NULL);
    mrt_kernel_note_cpu_time(NULL, 5);
    mrt_kernel_note_ctx_overhead(NULL, 5);
    mrt_kernel_note_idle(NULL, 5);
    TEST_ASSERT_EQ_INT(0, (int)mrt_kernel_task_count(NULL));
    TEST_ASSERT_EQ_INT(0, (int)mrt_kernel_resource_count(NULL));
    mrt_kernel_note_ctx_switch(&K);
    mrt_kernel_note_ctx_switch(&K);
    mrt_kernel_note_preemption(&K);
    mrt_kernel_note_cpu_time(&K, 7);
    mrt_kernel_note_ctx_overhead(&K, 3);
    mrt_kernel_note_idle(&K, 11);
    TEST_ASSERT_EQ_U64(2, K.ctx_switches);
    TEST_ASSERT_EQ_U64(1, K.preemptions);
    TEST_ASSERT_EQ_U64(7, K.cpu_busy_time);
    TEST_ASSERT_EQ_U64(3, K.ctx_overhead_time);
    TEST_ASSERT_EQ_U64(11, K.idle_time);

    /* ============ registration basics ============ */
    TEST_NAME("register-basic");
    reset();
    mrt_task_id_t a = reg("Alpha", 5);
    TEST_ASSERT_EQ_INT(0, (int)a);
    TEST_ASSERT_EQ_INT(1, (int)mrt_kernel_task_count(&K));
    mrt_task_t *ta = mrt_task_get(&K, a);
    TEST_ASSERT(ta != NULL);
    TEST_ASSERT(ta != NULL && strcmp(ta->name, "Alpha") == 0);
    TEST_ASSERT(ta != NULL && ta->state == MRT_TASK_UNUSED);
    TEST_ASSERT(ta != NULL && ta->base_priority == 5);
    TEST_ASSERT(ta != NULL && ta->effective_priority == 5);
    TEST_ASSERT(ta != NULL && ta->kind == MRT_TASK_APERIODIC);
    TEST_ASSERT(ta != NULL && ta->dispatched_once == 0);

    /* invalid arguments */
    mrt_task_id_t dummy = MRT_TASK_ID_NONE;
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_register(NULL, "x", MRT_TASK_APERIODIC, 0, 0, 0, 0, &dummy));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_register(&K, NULL, MRT_TASK_APERIODIC, 0, 0, 0, 0, &dummy));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_register(&K, "x", MRT_TASK_APERIODIC, 0, 0, 0, 0, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_register(&K, "x", (mrt_task_kind_t)99, 0, 0, 0, 0, &dummy));
    TEST_ASSERT(mrt_task_get(NULL, 0) == NULL);
    TEST_ASSERT(mrt_task_get(&K, MRT_TASK_ID_NONE) == NULL);
    TEST_ASSERT(mrt_task_get(&K, 1) == NULL);   /* not registered yet */

    /* fields round-trip */
    TEST_NAME("register-fields");
    reset();
    mrt_task_id_t p = MRT_TASK_ID_NONE;
    TEST_ASSERT_EQ_INT(MRT_OK,
        mrt_task_register(&K, "P", MRT_TASK_PERIODIC, 3, 7, 40, 30, &p));
    mrt_task_t *tp = mrt_task_get(&K, p);
    TEST_ASSERT(tp != NULL && tp->kind == MRT_TASK_PERIODIC);
    TEST_ASSERT(tp != NULL && tp->arrival == 7);
    TEST_ASSERT(tp != NULL && tp->period == 40);
    TEST_ASSERT(tp != NULL && tp->relative_deadline == 30);

    /* priority clamping to [MRT_PRIO_MIN, MRT_PRIO_MAX] */
    TEST_NAME("register-prio-clamp");
    reset();
    mrt_task_id_t hi = MRT_TASK_ID_NONE, lo = MRT_TASK_ID_NONE;
    TEST_ASSERT_EQ_INT(MRT_OK,
        mrt_task_register(&K, "Hi", MRT_TASK_APERIODIC, 500, 0, 0, 0, &hi));
    TEST_ASSERT_EQ_INT(MRT_OK,
        mrt_task_register(&K, "Lo", MRT_TASK_APERIODIC, -500, 0, 0, 0, &lo));
    TEST_ASSERT(mrt_task_get(&K, hi) != NULL &&
                mrt_task_get(&K, hi)->base_priority == MRT_PRIO_MAX);
    TEST_ASSERT(mrt_task_get(&K, lo) != NULL &&
                mrt_task_get(&K, lo)->base_priority == MRT_PRIO_MIN);

    /* ============ duplicates + name truncation ============ */
    TEST_NAME("register-duplicate");
    reset();
    (void)reg("Dup", 0);
    TEST_ASSERT_EQ_INT(MRT_ERR_DUPLICATE,
        mrt_task_register(&K, "Dup", MRT_TASK_APERIODIC, 1, 0, 0, 0, &dummy));
    /* 40-char names truncate at 31 chars: same prefix => duplicate */
    const char *long1 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; /* 38 chars */
    const char *long2 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbb"; /* same 31-char prefix */
    mrt_task_id_t l1 = MRT_TASK_ID_NONE;
    TEST_ASSERT_EQ_INT(MRT_OK,
        mrt_task_register(&K, long1, MRT_TASK_APERIODIC, 0, 0, 0, 0, &l1));
    TEST_ASSERT_EQ_INT(MRT_ERR_DUPLICATE,
        mrt_task_register(&K, long2, MRT_TASK_APERIODIC, 0, 0, 0, 0, &dummy));
    mrt_task_t *tl = mrt_task_get(&K, l1);
    TEST_ASSERT(tl != NULL && strlen(tl->name) == MRT_MAX_NAME - 1u);
    /* find matches through truncation */
    TEST_ASSERT_EQ_INT((int)l1, (int)mrt_task_find(&K, long1));
    TEST_ASSERT_EQ_INT((int)l1, (int)mrt_task_find(&K, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    /* find basics */
    TEST_ASSERT_EQ_INT(MRT_TASK_ID_NONE, (int)mrt_task_find(&K, "nope"));
    TEST_ASSERT_EQ_INT(MRT_TASK_ID_NONE, (int)mrt_task_find(NULL, "x"));
    TEST_ASSERT_EQ_INT(MRT_TASK_ID_NONE, (int)mrt_task_find(&K, NULL));

    /* ============ full table ============ */
    TEST_NAME("register-full");
    reset();
    for (uint32_t i = 0u; i < MRT_MAX_TASKS; i++) {
        char name[MRT_MAX_NAME];
        (void)snprintf(name, sizeof name, "T%02u", (unsigned)i);
        TEST_ASSERT_EQ_INT(MRT_OK,
            mrt_task_register(&K, name, MRT_TASK_APERIODIC, 1, 0, 0, 0, &dummy));
    }
    TEST_ASSERT_EQ_INT((int)MRT_MAX_TASKS, (int)mrt_kernel_task_count(&K));
    TEST_ASSERT_EQ_INT(MRT_ERR_FULL,
        mrt_task_register(&K, "Extra", MRT_TASK_APERIODIC, 1, 0, 0, 0, &dummy));
    TEST_ASSERT(mrt_task_get(&K, MRT_MAX_TASKS - 1u) != NULL);
    TEST_ASSERT(mrt_task_get(&K, MRT_MAX_TASKS) == NULL);

    /* ============ state machine: legal edges ============ */
    TEST_NAME("state-legal");
    reset();
    mrt_task_id_t t = reg("T", 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_READY, 5));
    TEST_ASSERT(mrt_task_get(&K, t)->state == MRT_TASK_READY);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_RUNNING, 9));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_READY, 10));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_RUNNING, 11));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_BLOCKED, 12));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_READY, 13));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_RUNNING, 14));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_WAITING, 15));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_READY, 16));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_RUNNING, 17));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_SLEEPING, 18));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_READY, 19));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_RUNNING, 20));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, t, MRT_TASK_TERMINATED, 21));

    /* ============ state machine: illegal edges ============ */
    TEST_NAME("state-illegal");
    reset();
    mrt_task_id_t x = reg("X", 4);
    /* UNUSED -> anything but READY */
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_BLOCKED, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_UNUSED, 0));
    /* READY -> BLOCKED/WAITING/SLEEPING/TERMINATED/READY */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_READY, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_BLOCKED, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_WAITING, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_SLEEPING, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_TERMINATED, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_READY, 1));
    TEST_ASSERT(mrt_task_get(&K, x)->state == MRT_TASK_READY); /* unchanged */
    /* BLOCKED -> RUNNING/BLOCKED */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 2));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_BLOCKED, 3));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 4));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_BLOCKED, 4));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_WAITING, 4));
    /* WAITING -> RUNNING */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_READY, 5));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 6));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_WAITING, 7));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 8));
    /* SLEEPING -> RUNNING */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_READY, 9));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 10));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_SLEEPING, 11));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 12));
    /* TERMINATED is terminal */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_READY, 13));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_RUNNING, 14));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, x, MRT_TASK_TERMINATED, 15));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_READY, 16));
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, x, MRT_TASK_UNUSED, 16));
    /* RUNNING -> RUNNING is the one legal no-op */
    TEST_NAME("state-running-noop");
    reset();
    mrt_task_id_t y = reg("Y", 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, y, MRT_TASK_READY, 0));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, y, MRT_TASK_RUNNING, 5));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, y, MRT_TASK_RUNNING, 9));
    TEST_ASSERT(mrt_task_get(&K, y)->state == MRT_TASK_RUNNING);
    TEST_ASSERT_EQ_U64(0, mrt_task_get(&K, y)->cpu_time_total); /* no accounting */
    /* invalid arguments */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_set_state(NULL, 0, MRT_TASK_READY, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_set_state(&K, 0, (mrt_task_state_t)99, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
        mrt_task_set_state(&K, 0, (mrt_task_state_t)(-1), 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND,
        mrt_task_set_state(&K, 42, MRT_TASK_READY, 0)); /* unknown id */

    /* ============ statistics accumulation ============ */
    TEST_NAME("stats-accumulation");
    reset();
    mrt_task_id_t s = reg("S", 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_READY, 5));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_RUNNING, 9));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_BLOCKED, 14));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_READY, 20));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_RUNNING, 22));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s, MRT_TASK_TERMINATED, 30));
    mrt_task_t *ts = mrt_task_get(&K, s);
    TEST_ASSERT_EQ_U64(6, ts->ready_wait_total);   /* (9-5) + (22-20) */
    TEST_ASSERT_EQ_U64(13, ts->cpu_time_total);    /* (14-9) + (30-22) */
    TEST_ASSERT_EQ_U64(6, ts->blocked_total);      /* 20-14 */
    TEST_ASSERT_EQ_U64(0, ts->waiting_total);
    TEST_ASSERT_EQ_U64(0, ts->sleeping_total);
    TEST_ASSERT_EQ_U64(9, ts->first_dispatch);
    TEST_ASSERT(ts->dispatched_once == 1u);
    TEST_ASSERT_EQ_U64(20, ts->last_ready_at);     /* last READY entry */
    TEST_ASSERT_EQ_U64(30, ts->state_entered_at);
    TEST_ASSERT_EQ_INT(1, (int)ts->jobs_released); /* UNUSED->READY once */
    TEST_ASSERT_EQ_INT(0, (int)ts->jobs_completed);/* engine-managed field */
    /* defensive clamp: non-monotone `now` contributes a zero delta */
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE,
        mrt_task_set_state(&K, s, MRT_TASK_READY, 10)); /* TERMINATED anyway */
    reset();
    mrt_task_id_t s2 = reg("S2", 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s2, MRT_TASK_READY, 4));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s2, MRT_TASK_RUNNING, 5));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, s2, MRT_TASK_READY, 3));
    TEST_ASSERT_EQ_U64(0, mrt_task_get(&K, s2)->cpu_time_total); /* clamped */

    /* ============ boost / recompute (no resources) ============ */
    TEST_NAME("boost-recompute");
    reset();
    mrt_task_id_t b = reg("B", 50);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, b, 10));
    TEST_ASSERT(mrt_task_get(&K, b)->effective_priority == 60);
    TEST_ASSERT(mrt_task_get(&K, b)->aging_units == 10u);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_recompute_priority(&K, b));
    TEST_ASSERT(mrt_task_get(&K, b)->effective_priority == 60); /* base+aging */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, b, -4));
    TEST_ASSERT(mrt_task_get(&K, b)->effective_priority == 56);
    TEST_ASSERT(mrt_task_get(&K, b)->aging_units == 6u);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_recompute_priority(&K, b));
    TEST_ASSERT(mrt_task_get(&K, b)->effective_priority == 56);
    /* clamp at MRT_PRIO_MAX */
    mrt_task_id_t c = reg("C", 99);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, c, 5));
    TEST_ASSERT(mrt_task_get(&K, c)->effective_priority == MRT_PRIO_MAX);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_recompute_priority(&K, c));
    TEST_ASSERT(mrt_task_get(&K, c)->effective_priority == MRT_PRIO_MAX);
    /* aging_units cannot go below zero */
    mrt_task_id_t d = reg("D", 10);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, d, -100));
    TEST_ASSERT(mrt_task_get(&K, d)->aging_units == 0u);
    TEST_ASSERT(mrt_task_get(&K, d)->effective_priority == 10);
    /* invalid args */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_task_boost(NULL, 0, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_task_recompute_priority(NULL, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_task_boost(&K, 99, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_task_recompute_priority(&K, 99));

    return TEST_SUMMARY();
}
