/* test_queue.c — ready queue: ordering, FIFO ties, reposition, remove,
 * wraparound, seq assignment.

Copyright © 2026 Parsa Fathi. Apache-2.0. */
#define TH_SUITE "test_queue"
#include "harness.h"
#include "micrort/mrt_kernel.h"

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
    /* ============ init / empty ============ */
    TEST_NAME("queue-empty");
    mrt_readyq_t q;
    mrt_readyq_init(&q);
    TEST_ASSERT_EQ_INT(0, (int)q.count);
    mrt_readyq_init(NULL); /* must not crash */
    reset();
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_readyq_peek(&K));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_readyq_pop(NULL));
    TEST_ASSERT_EQ_INT((int)MRT_TASK_ID_NONE, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(NULL));

    /* ============ FIFO within equal priority ============ */
    TEST_NAME("queue-fifo-equal-prio");
    reset();
    mrt_task_id_t a = reg("A", 5), b = reg("B", 5), c = reg("C", 5);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, a));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, b));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, c));
    TEST_ASSERT_EQ_INT(3, (int)mrt_readyq_size(&K));
    TEST_ASSERT_EQ_INT((int)a, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)b, (int)mrt_readyq_at(&K, 1));
    TEST_ASSERT_EQ_INT((int)c, (int)mrt_readyq_at(&K, 2));
    TEST_ASSERT_EQ_INT((int)a, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)b, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)c, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K));

    /* ============ priority order (strict DESC) ============ */
    TEST_NAME("queue-priority-order");
    reset();
    mrt_task_id_t low = reg("low", 1), high = reg("high", 10), mid = reg("mid", 5);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, low));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, high));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, mid));
    TEST_ASSERT_EQ_INT((int)high, (int)mrt_readyq_peek(&K));
    TEST_ASSERT_EQ_INT((int)high, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)mid, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)low, (int)mrt_readyq_pop(&K));
    /* late high-priority arrival jumps ahead of equal/lower residents */
    mrt_task_id_t x = reg("X", 5), y = reg("Y", 5), z = reg("Z", 7);
    mrt_readyq_push(&K, x); /* seq n */
    mrt_readyq_push(&K, y);
    mrt_readyq_push(&K, z); /* z higher -> head */
    TEST_ASSERT_EQ_INT((int)z, (int)mrt_readyq_pop(&K));
    TEST_ASSERT_EQ_INT((int)x, (int)mrt_readyq_pop(&K)); /* FIFO within 5 */
    TEST_ASSERT_EQ_INT((int)y, (int)mrt_readyq_pop(&K));

    /* ============ enqueue_seq assignment ============ */
    TEST_NAME("queue-seq");
    reset();
    mrt_task_id_t s1 = reg("s1", 0), s2 = reg("s2", 0);
    TEST_ASSERT_EQ_U64(1, K.next_seq); /* starts at 1 */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, s1));
    TEST_ASSERT_EQ_U64(1, mrt_task_get(&K, s1)->enqueue_seq);
    TEST_ASSERT_EQ_U64(2, K.next_seq);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, s2));
    TEST_ASSERT_EQ_U64(2, mrt_task_get(&K, s2)->enqueue_seq);
    (void)mrt_readyq_pop(&K);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, s1)); /* fresh stamp */
    TEST_ASSERT_EQ_U64(3, mrt_task_get(&K, s1)->enqueue_seq);

    /* ============ reposition after priority change ============ */
    TEST_NAME("queue-reposition");
    reset();
    mrt_task_id_t r1 = reg("r1", 5), r2 = reg("r2", 5), r3 = reg("r3", 5);
    mrt_readyq_push(&K, r1);
    mrt_readyq_push(&K, r2);
    mrt_readyq_push(&K, r3);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, r3, 2)); /* auto-repositions */
    TEST_ASSERT_EQ_INT((int)r3, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)r1, (int)mrt_readyq_at(&K, 1));
    TEST_ASSERT_EQ_INT((int)r2, (int)mrt_readyq_at(&K, 2));
    /* reposition explicitly (keeps the SAME enqueue_seq -> FIFO place) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_task_boost(&K, r3, -2)); /* back to 5 */
    TEST_ASSERT_EQ_INT((int)r1, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)r2, (int)mrt_readyq_at(&K, 1));
    TEST_ASSERT_EQ_INT((int)r3, (int)mrt_readyq_at(&K, 2)); /* seq 3 last */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_reposition(&K, r2)); /* same place */
    TEST_ASSERT_EQ_INT((int)r1, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)r2, (int)mrt_readyq_at(&K, 1));
    TEST_ASSERT_EQ_INT((int)r3, (int)mrt_readyq_at(&K, 2));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_readyq_reposition(&K, 99));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_readyq_reposition(NULL, r1));

    /* remove middle element */
    TEST_NAME("queue-remove-middle");
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_remove(&K, r2));
    TEST_ASSERT_EQ_INT(2, (int)mrt_readyq_size(&K));
    TEST_ASSERT_EQ_INT((int)r1, (int)mrt_readyq_at(&K, 0));
    TEST_ASSERT_EQ_INT((int)r3, (int)mrt_readyq_at(&K, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_readyq_remove(&K, r2)); /* gone */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_remove(&K, r1));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_remove(&K, r3));
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_readyq_remove(&K, r1));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_readyq_remove(NULL, r1));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_readyq_push(NULL, r1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_readyq_push(&K, 99));
    /* defensive duplicate push: r1 is queued -> MRT_ERR_DUPLICATE */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, r1));
    TEST_ASSERT_EQ_INT(MRT_ERR_DUPLICATE, mrt_readyq_push(&K, r1));
    (void)mrt_readyq_pop(&K);

    /* ============ wraparound: repeated push/pop cycles ============ */
    TEST_NAME("queue-wraparound");
    reset();
    mrt_task_id_t w1 = reg("w1", 3), w2 = reg("w2", 3), w3 = reg("w3", 3);
    for (uint32_t i = 0u; i < 300u; i++) {
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, w1));
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, w2));
        TEST_ASSERT_EQ_INT((int)w1, (int)mrt_readyq_pop(&K));
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, w3));
        TEST_ASSERT_EQ_INT((int)w2, (int)mrt_readyq_pop(&K));
        TEST_ASSERT_EQ_INT((int)w3, (int)mrt_readyq_pop(&K));
    }
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K)); /* head wrapped ~14x */

    /* ============ fill to capacity ============ */
    TEST_NAME("queue-fill-capacity");
    reset();
    mrt_task_id_t ids[MRT_MAX_TASKS];
    for (uint32_t i = 0u; i < MRT_MAX_TASKS; i++) {
        char name[MRT_MAX_NAME];
        (void)snprintf(name, sizeof name, "F%02u", (unsigned)i);
        ids[i] = reg(name, (mrt_prio_t)(i % 7));
    }
    for (uint32_t i = 0u; i < MRT_MAX_TASKS; i++) {
        TEST_ASSERT_EQ_INT(MRT_OK, mrt_readyq_push(&K, ids[i]));
    }
    TEST_ASSERT_EQ_INT((int)MRT_READYQ_CAPACITY, (int)mrt_readyq_size(&K));
    /* drain fully and verify strict priority DESC + FIFO ties */
    mrt_prio_t last = MRT_PRIO_MAX + 1;
    uint32_t popped = 0u;
    mrt_task_id_t prev = MRT_TASK_ID_NONE;
    for (;;) {
        mrt_task_id_t id = mrt_readyq_pop(&K);
        if (id == MRT_TASK_ID_NONE) {
            break;
        }
        mrt_task_t *t = mrt_task_get(&K, id);
        TEST_ASSERT(t != NULL);
        if (t != NULL) {
            TEST_ASSERT(t->effective_priority <= last);
            if (t->effective_priority == last && prev != MRT_TASK_ID_NONE) {
                mrt_task_t *pt = mrt_task_get(&K, prev);
                TEST_ASSERT(pt == NULL || pt->enqueue_seq < t->enqueue_seq);
            }
            last = t->effective_priority;
            prev = id;
        }
        popped++;
    }
    TEST_ASSERT_EQ_INT((int)MRT_MAX_TASKS, (int)popped);
    TEST_ASSERT_EQ_INT(0, (int)mrt_readyq_size(&K));

    return TEST_SUMMARY();
}
