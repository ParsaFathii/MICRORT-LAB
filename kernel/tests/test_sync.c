/* test_sync.c — mutex (hand-off + priority inheritance), semaphore,
 * message queue (FIFO wake) and event flags.

All blocking flows follow the legal state machine: a task must be RUNNING
to block (RUNNING->BLOCKED) and wakes to READY (the ENGINE then pushes it
into the ready queue — the kernel never auto-queues, see mrt_sync.c).

Copyright © 2026 Parsa Fathi. Apache-2.0. */
#define TH_SUITE "test_sync"
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

static mrt_task_state_t state_of(mrt_task_id_t id)
{
    mrt_task_t *t = mrt_task_get(&K, id);
    TEST_ASSERT(t != NULL);
    return (t != NULL) ? t->state : MRT_TASK_UNUSED;
}

/* release (+ optional re-ready) + dispatch: leaves the task RUNNING. */
static void make_running(mrt_task_id_t id, mrt_time_t t)
{
    K.now = t;
    if (state_of(id) != MRT_TASK_READY) {
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, id, MRT_TASK_READY, t));
    }
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, id, MRT_TASK_RUNNING, t));
}

int main(void)
{
    uint32_t mx = 0u, mxn = 0u, sem = 0u, q = 0u, ev = 0u, found = 0u;
    int acq = -1, sent = -1, got = -1, sat = -1;
    int32_t val = -1;
    int32_t buf[4] = {0, 0, 0, 0};
    mrt_task_id_t handed = MRT_TASK_ID_NONE, wok = MRT_TASK_ID_NONE;
    mrt_task_id_t wok_arr[4];
    uint32_t wcount = 99u;

    /* ============ resource registration ============ */
    TEST_NAME("res-register");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "Mx", MRT_PROTO_INHERIT, &mx));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "MxN", MRT_PROTO_NONE, &mxn));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_sem(&K, "S", 2, 3, &sem));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_evflags(&K, "E", &ev));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "Q", 4, buf, &q));
    TEST_ASSERT_EQ_INT(5, (int)mrt_kernel_resource_count(&K));
    TEST_ASSERT(mrt_res_get(&K, mx) != NULL &&
                mrt_res_get(&K, mx)->kind == MRT_RES_MUTEX &&
                mrt_res_get(&K, mx)->protocol == MRT_PROTO_INHERIT);
    TEST_ASSERT(mrt_res_get(&K, sem) != NULL && mrt_res_get(&K, sem)->sem_count == 2);
    TEST_ASSERT(mrt_res_get(&K, sem) != NULL && mrt_res_get(&K, sem)->sem_max == 3);
    TEST_ASSERT(mrt_res_get(&K, q) != NULL && mrt_res_get(&K, q)->msg_capacity == 4);
    TEST_ASSERT(mrt_res_get(&K, ev) != NULL && mrt_res_get(&K, ev)->evflags == 0u);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_find(&K, "Mx", &found));
    TEST_ASSERT_EQ_INT((int)mx, (int)found);
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_res_find(&K, "nope", &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_DUPLICATE,
        mrt_res_register_mutex(&K, "Mx", MRT_PROTO_NONE, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_mutex(NULL, "x", MRT_PROTO_NONE, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_mutex(&K, NULL, MRT_PROTO_NONE, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_mutex(&K, "x", MRT_PROTO_NONE, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_mutex(&K, "x", (mrt_mutex_proto_t)7, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_sem(&K, "S2", 0, 0, &found));   /* max < 1 */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_sem(&K, "S3", -1, 2, &found));  /* init < 0 */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_sem(&K, "S4", 3, 2, &found));   /* init > max */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_msgq(&K, "Q2", 0, buf, &found)); /* cap < 1 */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_msgq(&K, "Q3", 4, NULL, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_register_evflags(NULL, "x", &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_find(NULL, "x", &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_find(&K, NULL, &found));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_res_find(&K, "x", NULL));
    TEST_ASSERT(mrt_res_get(&K, 99u) == NULL);
    TEST_ASSERT(mrt_res_get(NULL, 0u) == NULL);
    /* full table */
    reset();
    for (uint32_t i = 0u; i < MRT_MAX_RESOURCES; i++) {
        char name[MRT_MAX_NAME];
        (void)snprintf(name, sizeof name, "R%02u", (unsigned)i);
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, name, MRT_PROTO_NONE, &found));
    }
    TEST_ASSERT_EQ_INT(MRT_ERR_FULL, mrt_res_register_mutex(&K, "Extra", MRT_PROTO_NONE, &found));

    /* ============ mutex: basic lock/unlock ============ */
    TEST_NAME("mutex-basic");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx));
    mrt_task_id_t t1 = reg("T1", 5), t2 = reg("T2", 3);
    make_running(t1, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, t1, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT(mrt_res_get(&K, mx)->owner == t1);
    TEST_ASSERT_EQ_INT(1, (int)mrt_res_get(&K, mx)->acquisitions);
    TEST_ASSERT(state_of(t1) == MRT_TASK_RUNNING); /* state unchanged */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, t1, &handed));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)handed);
    TEST_ASSERT(mrt_res_get(&K, mx)->owner == MRT_TASK_ID_NONE);
    /* non-owner unlock + unlock of a free mutex */
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_OWNER, mrt_mutex_unlock(&K, mx, t2, &handed));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_OWNER, mrt_mutex_unlock(&K, mx, t1, &handed));
    /* relock by the owner -> BAD_STATE (RAG self-cycle is engine's call) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, t1, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT_EQ_INT(MRT_ERR_BAD_STATE, mrt_mutex_lock(&K, mx, t1, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, t1, &handed));

    /* ============ mutex: contention blocks + hand-off ============ */
    TEST_NAME("mutex-contention");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx));
    t1 = reg("T1", 5);
    t2 = reg("T2", 3);
    make_running(t1, 1);
    make_running(t2, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, t1, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, t2, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT(state_of(t2) == MRT_TASK_BLOCKED);
    TEST_ASSERT_EQ_INT(1, (int)mrt_res_get(&K, mx)->contentions);
    TEST_ASSERT_EQ_INT((int)t2, (int)mrt_mutex_next_waiter(&K, mx));
    /* unlock hands the mutex to the waiter: it becomes owner + READY */
    K.now = 10; /* blocked_total stamps via k->now (header amendment) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, t1, &handed));
    TEST_ASSERT_EQ_INT((int)t2, (int)handed);
    TEST_ASSERT(state_of(t2) == MRT_TASK_READY);
    TEST_ASSERT(mrt_res_get(&K, mx)->owner == t2);
    TEST_ASSERT_EQ_INT(2, (int)mrt_res_get(&K, mx)->acquisitions); /* + hand-off */
    TEST_ASSERT_EQ_INT(0, (int)mrt_res_get(&K, mx)->waiter_count);
    TEST_ASSERT_EQ_U64(8, mrt_task_get(&K, t2)->blocked_total); /* 10-2 */
    TEST_ASSERT_EQ_U64(10, mrt_task_get(&K, t2)->last_ready_at);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, t2, &handed));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)handed);

    /* ============ mutex: waiter ordering (prio, FIFO ties) ============ */
    TEST_NAME("mutex-waiter-order");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx));
    mrt_task_id_t o = reg("O", 9);
    mrt_task_id_t w3 = reg("w3", 3), w5 = reg("w5", 5), w2 = reg("w2", 2), w4 = reg("w4", 4);
    make_running(o, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, o, &acq));
    make_running(w3, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, w3, &acq)); /* blocked first */
    make_running(w5, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, w5, &acq));
    make_running(w2, 3);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, w2, &acq));
    make_running(w4, 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, w4, &acq));
    TEST_ASSERT_EQ_INT(4, (int)mrt_res_get(&K, mx)->waiter_count);
    TEST_ASSERT_EQ_INT((int)w5, (int)mrt_mutex_next_waiter(&K, mx)); /* highest */
    K.now = 5;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, o, &handed));
    TEST_ASSERT_EQ_INT((int)w5, (int)handed);
    TEST_ASSERT_EQ_INT((int)w4, (int)mrt_mutex_next_waiter(&K, mx));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, w5, &handed));
    TEST_ASSERT_EQ_INT((int)w4, (int)handed);
    TEST_ASSERT_EQ_INT((int)w3, (int)mrt_mutex_next_waiter(&K, mx));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, w4, &handed));
    TEST_ASSERT_EQ_INT((int)w3, (int)handed); /* w3(3) beats w2(2) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, w3, &handed));
    TEST_ASSERT_EQ_INT((int)w2, (int)handed);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, w2, &handed));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)handed);
    /* equal-priority waiters: FIFO (first blocked = first handed) */
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx));
    mrt_task_id_t e1 = reg("e1", 4), e2 = reg("e2", 4);
    o = reg("O", 9);
    make_running(o, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, o, &acq));
    make_running(e1, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, e1, &acq));
    make_running(e2, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, e2, &acq));
    K.now = 3;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, o, &handed));
    TEST_ASSERT_EQ_INT((int)e1, (int)handed);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, e1, &handed));
    TEST_ASSERT_EQ_INT((int)e2, (int)handed);

    /* ============ mutex: priority inheritance (classic inversion) ==== */
    TEST_NAME("mutex-inherit");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "Mx", MRT_PROTO_INHERIT, &mx));
    mrt_task_id_t lo = reg("Low", 1), mid = reg("Mid", 5), hi = reg("High", 10);
    /* engine flow: L runs and locks Mx; L preempted; M ready; H preempts;
     * H blocks on Mx -> L inherits 10 and jumps ahead of M in the queue. */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, lo, MRT_TASK_READY, 0));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, lo));
    TEST_ASSERT_EQ_INT((int)lo, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, lo, MRT_TASK_RUNNING, 0));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, lo, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    /* M released at t=1; L preempted at t=2 -> queue [M(5), L(1)] */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, mid, MRT_TASK_READY, 1));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, mid));
    K.now = 2;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, lo, MRT_TASK_READY, 2));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, lo));
    TEST_ASSERT_EQ_INT((int)mid, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)lo, (int)mrt_readyq_at(&K, 1));
    /* H released at t=3, dispatched at t=4 (preempts M in engine terms) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, hi, MRT_TASK_READY, 3));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, hi));
    K.now = 4;
    TEST_ASSERT_EQ_INT((int)hi, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, hi, MRT_TASK_RUNNING, 4));
    /* H blocks on Mx: L must be boosted to 10 and REPOSITIONED before M */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, hi, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT(state_of(hi) == MRT_TASK_BLOCKED);
    TEST_ASSERT_EQ_INT(10, mrt_task_get(&K, lo)->effective_priority); /* boosted */
    TEST_ASSERT_EQ_INT((int)lo, (int)mrt_readyq_at(&K, 0)); /* repositioned */
    TEST_ASSERT_EQ_INT((int)mid, (int)mrt_readyq_at(&K, 1));
    /* L runs again and unlocks: hand-off to H, L rolls back to 1 */
    K.now = 9;
    TEST_ASSERT_EQ_INT((int)lo, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, lo, MRT_TASK_RUNNING, 9));
    K.now = 12;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, lo, &handed));
    TEST_ASSERT_EQ_INT((int)hi, (int)handed);
    TEST_ASSERT_EQ_INT(1, mrt_task_get(&K, lo)->effective_priority); /* rollback */
    TEST_ASSERT(mrt_res_get(&K, mx)->owner == hi);
    TEST_ASSERT(state_of(hi) == MRT_TASK_READY);
    TEST_ASSERT_EQ_U64(8, mrt_task_get(&K, hi)->blocked_total); /* 12-4 */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, hi, &handed));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)handed);
    TEST_ASSERT_EQ_INT(10, mrt_task_get(&K, hi)->effective_priority); /* base */

    /* ============ inheritance: third waiter, hand-off to the best ==== */
    TEST_NAME("mutex-inherit-third-waiter");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "Mx", MRT_PROTO_INHERIT, &mx));
    lo = reg("Low", 1);
    hi = reg("High", 10);
    mrt_task_id_t hi2 = reg("High2", 7);
    make_running(lo, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, lo, &acq));
    make_running(hi2, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, hi2, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT_EQ_INT(7, mrt_task_get(&K, lo)->effective_priority); /* raised */
    make_running(hi, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, hi, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT_EQ_INT(10, mrt_task_get(&K, lo)->effective_priority); /* max */
    K.now = 5;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, lo, &handed));
    TEST_ASSERT_EQ_INT((int)hi, (int)handed); /* highest waiter gets it */
    TEST_ASSERT_EQ_INT(1, mrt_task_get(&K, lo)->effective_priority);  /* rollback */
    /* new owner's base (10) still beats remaining waiter (7) */
    TEST_ASSERT_EQ_INT(10, mrt_task_get(&K, hi)->effective_priority);
    K.now = 6;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, hi, &handed));
    TEST_ASSERT_EQ_INT((int)hi2, (int)handed);
    TEST_ASSERT_EQ_INT(10, mrt_task_get(&K, hi)->effective_priority);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, hi2, &handed));

    /* ============ inheritance: NONE protocol never boosts ============ */
    TEST_NAME("mutex-no-inherit");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "Mn", MRT_PROTO_NONE, &mx));
    lo = reg("Low", 1);
    hi = reg("High", 10);
    make_running(lo, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, lo, &acq));
    make_running(hi, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_lock(&K, mx, hi, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT_EQ_INT(1, mrt_task_get(&K, lo)->effective_priority); /* unchanged */
    K.now = 2;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mutex_unlock(&K, mx, lo, &handed));
    TEST_ASSERT_EQ_INT((int)hi, (int)handed);
    TEST_ASSERT_EQ_INT(1, mrt_task_get(&K, lo)->effective_priority);

    /* ============ mutex: invalid args / kind mismatches ============ */
    TEST_NAME("mutex-invalid");
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_sem(&K, "S", 1, 1, &sem)); /* for kind test */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mutex_lock(NULL, 0, 0, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mutex_lock(&K, mx, 0, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mutex_lock(&K, 99u, 0, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mutex_lock(&K, mx, 99, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mutex_unlock(NULL, 0, 0, &handed));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mutex_unlock(&K, mx, 0, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mutex_unlock(&K, 99u, 0, &handed));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mutex_lock(&K, sem, 0, &acq)); /* wrong kind */
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_mutex_next_waiter(NULL, 0));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_mutex_next_waiter(&K, 99u));

    /* ============ semaphore ============ */
    TEST_NAME("sem-basic");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_sem(&K, "S", 2, 3, &sem));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx));
    mrt_task_id_t c1 = reg("C1", 5), c2 = reg("C2", 5), c3 = reg("C3", 3), c4 = reg("C4", 10);
    make_running(c1, 0);
    make_running(c2, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_wait(&K, sem, c1, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_wait(&K, sem, c2, &acq));
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 0);
    TEST_ASSERT_EQ_INT(2, (int)mrt_res_get(&K, sem)->acquisitions);
    /* zero count: blocks; waiters are priority-ordered */
    make_running(c3, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_wait(&K, sem, c3, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT(state_of(c3) == MRT_TASK_BLOCKED);
    make_running(c4, 3);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_wait(&K, sem, c4, &acq));
    TEST_ASSERT_EQ_INT(0, acq);
    TEST_ASSERT_EQ_INT(2, (int)mrt_res_get(&K, sem)->contentions);
    /* signal: token handed to the HIGHEST waiter, count unchanged */
    K.now = 8;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok));
    TEST_ASSERT_EQ_INT((int)c4, (int)wok);
    TEST_ASSERT(state_of(c4) == MRT_TASK_READY);
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 0); /* hand-off, no count */
    TEST_ASSERT_EQ_INT(3, (int)mrt_res_get(&K, sem)->acquisitions); /* wait done */
    TEST_ASSERT_EQ_U64(5, mrt_task_get(&K, c4)->blocked_total); /* 8-3 */
    /* signal with no waiters increments up to max, then overflows
     * (c3 was STILL a waiter: the second signal hands the token to c3
     * rather than raising the count — hand-off semantics) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok));
    TEST_ASSERT_EQ_INT((int)c3, (int)wok);
    TEST_ASSERT(state_of(c3) == MRT_TASK_READY);
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 0); /* token handed */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok)); /* 0->1 */
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)wok);
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok)); /* 1->2 */
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok)); /* 2->3 = max */
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 3);
    TEST_ASSERT_EQ_INT(MRT_ERR_OVERFLOW, mrt_sem_signal(&K, sem, &wok));
    TEST_ASSERT(mrt_res_get(&K, sem)->sem_count == 3); /* stays clamped */
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)wok);
    /* drain a token through a direct wait */
    make_running(c1, 20);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_wait(&K, sem, c1, &acq)); /* 3->2 */
    TEST_ASSERT_EQ_INT(1, acq);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_sem_signal(&K, sem, &wok));   /* 2->3 */
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)wok);
    /* invalid args */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_sem_wait(NULL, 0, 0, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_sem_wait(&K, sem, 0, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_sem_wait(&K, 99u, 0, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_sem_wait(&K, sem, 99, &acq));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_sem_signal(NULL, 0, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_sem_signal(&K, sem, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_sem_signal(&K, 99u, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_sem_wait(&K, mx, 0, &acq)); /* wrong kind */

    /* ============ message queue ============ */
    TEST_NAME("msgq-basic");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "Q", 2, buf, &q));
    mrt_task_id_t prod = reg("P", 5), cons = reg("C", 5);
    make_running(prod, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, prod, 10, &sent, &wok));
    TEST_ASSERT_EQ_INT(1, sent);
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)wok);
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, prod, 20, &sent, &wok));
    TEST_ASSERT_EQ_INT(1, sent);
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 2); /* full now */
    /* full queue: sender blocks (FIFO waiters) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, prod, 30, &sent, &wok));
    TEST_ASSERT_EQ_INT(0, sent);
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)wok);
    TEST_ASSERT(state_of(prod) == MRT_TASK_BLOCKED);
    TEST_ASSERT_EQ_INT(1, (int)mrt_res_get(&K, q)->contentions);
    /* recv dequeues FIFO and wakes the blocked sender */
    make_running(cons, 5);
    K.now = 6;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, cons, &val, &got));
    TEST_ASSERT_EQ_INT(1, got);
    TEST_ASSERT_EQ_INT(10, (int)val);
    TEST_ASSERT(state_of(prod) == MRT_TASK_READY); /* woken to retry send */
    TEST_ASSERT_EQ_INT(0, (int)mrt_res_get(&K, q)->waiter_count);
    /* sender retries (dispatched again) and succeeds */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, prod, MRT_TASK_RUNNING, 7));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, prod, 30, &sent, &wok));
    TEST_ASSERT_EQ_INT(1, sent);
    /* drain in FIFO order */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, cons, &val, &got));
    TEST_ASSERT_EQ_INT(20, (int)val);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, cons, &val, &got));
    TEST_ASSERT_EQ_INT(30, (int)val);
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 0);
    /* empty queue: receiver blocks */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, cons, &val, &got));
    TEST_ASSERT_EQ_INT(0, got);
    TEST_ASSERT_EQ_INT(0, (int)val); /* deterministic zero on block */
    TEST_ASSERT(state_of(cons) == MRT_TASK_BLOCKED);
    /* send wakes the FIFO-first receiver; payload stays queued */
    K.now = 9;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, prod, 50, &sent, &wok));
    TEST_ASSERT_EQ_INT(1, sent);
    TEST_ASSERT_EQ_INT((int)cons, (int)wok);
    TEST_ASSERT(state_of(cons) == MRT_TASK_READY);
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 1); /* not consumed yet */
    TEST_ASSERT_EQ_U64(3, mrt_task_get(&K, cons)->blocked_total); /* 9-6 */
    /* receiver resumes and dequeues */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_set_state(&K, cons, MRT_TASK_RUNNING, 10));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, cons, &val, &got));
    TEST_ASSERT_EQ_INT(1, got);
    TEST_ASSERT_EQ_INT(50, (int)val);

    TEST_NAME("msgq-fifo-wake-order");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "Q", 4, buf, &q));
    mrt_task_id_t r1 = reg("r1", 1), r2 = reg("r2", 9); /* prio ignored for msgq */
    mrt_task_id_t s1 = reg("s1", 1), s2 = reg("s2", 9);
    /* two receivers block on empty queue in arrival order */
    make_running(r1, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, r1, &val, &got));
    TEST_ASSERT_EQ_INT(0, got);
    make_running(r2, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, r2, &val, &got));
    TEST_ASSERT_EQ_INT(0, got);
    /* send wakes the FIRST arrival (r1) even though r2 has higher prio */
    make_running(s2, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s2, 7, &sent, &wok));
    TEST_ASSERT_EQ_INT((int)r1, (int)wok);
    TEST_ASSERT(state_of(r1) == MRT_TASK_READY);
    TEST_ASSERT(state_of(r2) == MRT_TASK_BLOCKED); /* stays blocked */
    /* fill the queue (capacity 4): 3 more sends, then senders block */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s2, 8, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s2, 9, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s2, 11, &sent, &wok));
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 4);
    make_running(s1, 3);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s1, 12, &sent, &wok)); /* full */
    TEST_ASSERT_EQ_INT(0, sent);
    TEST_ASSERT(state_of(s1) == MRT_TASK_BLOCKED);
    make_running(s2, 4);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, s2, 13, &sent, &wok)); /* full */
    TEST_ASSERT_EQ_INT(0, sent);
    TEST_ASSERT(state_of(s2) == MRT_TASK_BLOCKED);
    /* recv wakes blocked senders FIFO-first (s1 blocked before s2) */
    mrt_task_id_t tmp = reg("tmp", 0);
    make_running(tmp, 5);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, tmp, &val, &got));
    TEST_ASSERT_EQ_INT(1, got);
    TEST_ASSERT_EQ_INT(7, (int)val);
    TEST_ASSERT(state_of(s1) == MRT_TASK_READY);  /* first blocked sender */
    TEST_ASSERT(state_of(s2) == MRT_TASK_BLOCKED);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, tmp, &val, &got));
    TEST_ASSERT_EQ_INT(8, (int)val);
    TEST_ASSERT(state_of(s2) == MRT_TASK_READY);

    TEST_NAME("msgq-wraparound");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "Q", 2, buf, &q));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_evflags(&K, "E", &ev)); /* kind test */
    mrt_task_id_t w = reg("w", 0);
    make_running(w, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, w, 1, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, w, 2, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, w, &val, &got));
    TEST_ASSERT_EQ_INT(1, (int)val);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, w, 3, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, w, &val, &got));
    TEST_ASSERT_EQ_INT(2, (int)val);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, w, &val, &got));
    TEST_ASSERT_EQ_INT(3, (int)val); /* ring wrapped head 0->1->0 */
    /* peek */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_send(&K, q, w, 99, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_peek(&K, q, &val));
    TEST_ASSERT_EQ_INT(99, (int)val);
    TEST_ASSERT(mrt_res_get(&K, q)->msg_count == 1); /* peek keeps data */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_msgq_recv(&K, q, w, &val, &got));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_msgq_peek(&K, q, &val));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_peek(NULL, 0, &val));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_peek(&K, q, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_msgq_peek(&K, 99u, &val));
    /* invalid args */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_send(NULL, 0, 0, 1, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_send(&K, q, 0, 1, NULL, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_send(&K, q, 0, 1, &sent, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_msgq_send(&K, 99u, 0, 1, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_msgq_send(&K, q, 99, 1, &sent, &wok));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_recv(NULL, 0, 0, &val, &got));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_recv(&K, q, 0, NULL, &got));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_recv(&K, q, 0, &val, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_msgq_recv(&K, 99u, 0, &val, &got));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_msgq_send(&K, ev, 0, 1, &sent, &wok)); /* kind */

    /* ============ event flags ============ */
    TEST_NAME("evflags-basic");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_evflags(&K, "E", &ev));
    mrt_task_id_t wa = reg("wa", 5), wb = reg("wb", 5);
    /* ANY waiter (mask 0b0011) blocks on empty flags */
    make_running(wa, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, wa, 0x3u, 0, &sat));
    TEST_ASSERT_EQ_INT(0, sat);
    TEST_ASSERT(state_of(wa) == MRT_TASK_BLOCKED);
    TEST_ASSERT_EQ_U64(0x3u, mrt_task_get(&K, wa)->engine_ctx); /* packed mask */
    TEST_ASSERT_EQ_INT(1, (int)mrt_res_get(&K, ev)->contentions);
    /* ALL waiter (mask 0b0111) also blocks; mode packed in bit 32 */
    make_running(wb, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, wb, 0x7u, 1, &sat));
    TEST_ASSERT_EQ_INT(0, sat);
    TEST_ASSERT_EQ_U64(0x7u | (UINT64_C(1) << 32),
                       mrt_task_get(&K, wb)->engine_ctx);
    /* set 0b0001: ANY waiter satisfied, ALL waiter not */
    K.now = 4;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x1u, wok_arr, 4u, &wcount));
    TEST_ASSERT_EQ_INT(1, (int)wcount);
    TEST_ASSERT_EQ_INT((int)wa, (int)wok_arr[0]);
    TEST_ASSERT(state_of(wa) == MRT_TASK_READY);
    TEST_ASSERT(state_of(wb) == MRT_TASK_BLOCKED); /* unsatisfied stays */
    TEST_ASSERT_EQ_U64(4, mrt_task_get(&K, wa)->blocked_total); /* 4-0 */
    /* set 0b0110: ALL waiter now satisfied (flags = 0b0111) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x6u, wok_arr, 4u, &wcount));
    TEST_ASSERT_EQ_INT(1, (int)wcount);
    TEST_ASSERT_EQ_INT((int)wb, (int)wok_arr[0]);
    TEST_ASSERT(mrt_res_get(&K, ev)->evflags == 0x7u);
    TEST_ASSERT_EQ_INT(0, (int)mrt_res_get(&K, ev)->waiter_count);
    /* acquisitions: 2 wakes + 1 later immediate wait = 3 */
    TEST_ASSERT_EQ_INT(2, (int)mrt_res_get(&K, ev)->acquisitions);
    /* clear bits */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_clear(&K, ev, 0x7u));
    TEST_ASSERT(mrt_res_get(&K, ev)->evflags == 0x0u);
    /* immediate satisfaction: no state change */
    make_running(wa, 10);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x2u, wok_arr, 4u, &wcount));
    TEST_ASSERT_EQ_INT(0, (int)wcount);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, wa, 0x2u, 0, &sat));
    TEST_ASSERT_EQ_INT(1, sat);
    TEST_ASSERT(state_of(wa) == MRT_TASK_RUNNING); /* unchanged */
    TEST_ASSERT_EQ_INT(3, (int)mrt_res_get(&K, ev)->acquisitions);
    /* ALL mode with partially-present mask: (flags & mask) != mask */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, wa, 0x3u, 1, &sat));
    TEST_ASSERT_EQ_INT(0, sat); /* flags = 0x2 only */
    TEST_ASSERT(state_of(wa) == MRT_TASK_BLOCKED);

    TEST_NAME("evflags-multi-wake");
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_evflags(&K, "E", &ev));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "Q", 2, buf, &q));  /* kind */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_mutex(&K, "M", MRT_PROTO_NONE, &mx)); /* kind */
    mrt_task_id_t m1 = reg("m1", 1), m2 = reg("m2", 2), m3 = reg("m3", 3);
    make_running(m1, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m1, 0x1u, 0, &sat));
    TEST_ASSERT_EQ_INT(0, sat);
    make_running(m2, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m2, 0x1u, 0, &sat));
    TEST_ASSERT_EQ_INT(0, sat);
    make_running(m3, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m3, 0x5u, 0, &sat)); /* bit 0 or 2 */
    TEST_ASSERT_EQ_INT(0, sat);
    /* set bit 0: all three ANY-waiters are satisfied simultaneously */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x1u, wok_arr, 4u, &wcount));
    TEST_ASSERT_EQ_INT(3, (int)wcount);
    TEST_ASSERT_EQ_INT((int)m1, (int)wok_arr[0]);
    TEST_ASSERT_EQ_INT((int)m2, (int)wok_arr[1]);
    TEST_ASSERT_EQ_INT((int)m3, (int)wok_arr[2]);
    /* woken_cap smaller than wake count: array truncated, count = total */
    reset();
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_evflags(&K, "E", &ev));
    m1 = reg("m1", 1);
    m2 = reg("m2", 2);
    m3 = reg("m3", 3);
    make_running(m1, 0);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m1, 0x1u, 0, &sat));
    make_running(m2, 1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m2, 0x1u, 0, &sat));
    make_running(m3, 2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_wait(&K, ev, m3, 0x1u, 0, &sat));
    mrt_task_id_t small[2];
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x1u, small, 2u, &wcount));
    TEST_ASSERT_EQ_INT(3, (int)wcount);
    TEST_ASSERT_EQ_INT((int)m1, (int)small[0]);
    TEST_ASSERT_EQ_INT((int)m2, (int)small[1]);
    /* zero cap with NULL array is legal */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_evflags_set(&K, ev, 0x2u, NULL, 0u, &wcount));
    TEST_ASSERT_EQ_INT(0, (int)wcount);
    /* invalid args */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_set(NULL, 0, 1, wok_arr, 4, &wcount));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_set(&K, ev, 1, wok_arr, 4, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_set(&K, ev, 1, NULL, 4, &wcount));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_evflags_set(&K, 99u, 1, wok_arr, 4, &wcount));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_wait(NULL, 0, 0, 1, 0, &sat));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_wait(&K, ev, 0, 1, 0, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_evflags_wait(&K, 99u, 0, 1, 0, &sat));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_evflags_wait(&K, ev, 99, 1, 0, &sat));
    /* kind checks need live resources of the wrong kind (the pre-reset
     * indices are stale here — NOT_FOUND, not INVALID_ARG) */
    {
        uint32_t mxx = 0u, qx = 0u;
        static int32_t qbuf[2];
        TEST_ASSERT_EQ_INT(MRT_OK,
            mrt_res_register_mutex(&K, "MX2", MRT_PROTO_NONE, &mxx));
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_res_register_msgq(&K, "QX", 2, qbuf, &qx));
        TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
            mrt_evflags_wait(&K, qx, 0, 1, 0, &sat)); /* msgq, wrong kind */
        TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG,
            mrt_evflags_clear(&K, mxx, 1)); /* mutex, wrong kind */
    }
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_evflags_clear(NULL, 0, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_evflags_clear(&K, 99u, 1));

    return TEST_SUMMARY();
}
