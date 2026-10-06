/* test_timer.c — sorted one-shot timers: ordering, stability, cancel,
 * pop_expired gating, cancel_all_for, error paths.

Copyright © 2026 Parsa Fathi. Apache-2.0. */
#define TH_SUITE "test_timer"
#include "harness.h"
#include "micrort/mrt_timer.h"

int main(void)
{
    /* ============ empty list ============ */
    TEST_NAME("timer-empty");
    mrt_timer_list_t l;
    mrt_timer_list_init(&l);
    TEST_ASSERT_EQ_INT(0, (int)l.count);
    TEST_ASSERT_EQ_U64((unsigned long long)MRT_TIME_MAX,
                       (unsigned long long)mrt_timer_next(&l));
    mrt_timer_t out;
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND,
                       mrt_timer_pop_expired(&l, 100, &out));
    mrt_timer_list_init(NULL); /* must not crash */
    TEST_ASSERT_EQ_U64((unsigned long long)MRT_TIME_MAX,
                       (unsigned long long)mrt_timer_next(NULL));

    /* ============ insertion keeps ascending order ============ */
    TEST_NAME("timer-order");
    mrt_timer_list_init(&l);
    uint32_t h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_add(&l, 30, 1, MRT_TIMER_TAG_WAKE, &h1));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_add(&l, 10, 2, MRT_TIMER_TAG_IO, &h2));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_add(&l, 20, 3, MRT_TIMER_TAG_DEADLINE, &h3));
    TEST_ASSERT_EQ_INT(3, (int)l.count);
    TEST_ASSERT_EQ_U64(10, (unsigned long long)mrt_timer_next(&l));
    TEST_ASSERT_EQ_INT(2, (int)l.items[0].task); /* head is t=10 task 2 */
    TEST_ASSERT_EQ_INT(3, (int)l.items[1].task);
    TEST_ASSERT_EQ_INT(1, (int)l.items[2].task);

    /* stability: equal fire_at keeps existing entry first */
    TEST_NAME("timer-stability");
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_add(&l, 10, 4, MRT_TIMER_TAG_AGING, &h4));
    TEST_ASSERT_EQ_INT(2, (int)l.items[0].task); /* task 2 still first */
    TEST_ASSERT_EQ_INT(4, (int)l.items[1].task); /* newcomer behind it */

    /* ============ pop_expired gating ============ */
    TEST_NAME("timer-pop-gating");
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND,
                       mrt_timer_pop_expired(&l, 9, &out)); /* not due yet */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_pop_expired(&l, 10, &out));
    TEST_ASSERT_EQ_INT(2, (int)out.task);
    TEST_ASSERT_EQ_INT((int)MRT_TIMER_TAG_IO, (int)out.tag);
    TEST_ASSERT_EQ_U64(10, (unsigned long long)out.fire_at);
    TEST_ASSERT_EQ_INT(3, (int)l.count);
    /* equal-time pop: insertion order again */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_pop_expired(&l, 10, &out));
    TEST_ASSERT_EQ_INT(4, (int)out.task);
    TEST_ASSERT_EQ_INT(2, (int)l.count);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_pop_expired(&l, 100, &out));
    TEST_ASSERT_EQ_INT(3, (int)out.task);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_pop_expired(&l, 100, &out));
    TEST_ASSERT_EQ_INT(1, (int)out.task);
    TEST_ASSERT_EQ_INT(0, (int)l.count);

    /* ============ cancel ============ */
    TEST_NAME("timer-cancel");
    mrt_timer_list_init(&l);
    mrt_timer_add(&l, 5, 10, MRT_TIMER_TAG_WAKE, &h1);   /* handle 1 */
    mrt_timer_add(&l, 6, 11, MRT_TIMER_TAG_WAKE, &h2);   /* handle 2 */
    mrt_timer_add(&l, 7, 12, MRT_TIMER_TAG_IO, &h3);     /* handle 3 */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_timer_cancel(NULL, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_timer_cancel(&l, 0));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_timer_cancel(&l, 99));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_cancel(&l, 2)); /* removes task 11 */
    TEST_ASSERT_EQ_INT(2, (int)l.count);
    /* compaction shifted handle 3's entry; verify state via the documented
     * rescan approach (no cached handles across mutations) */
    TEST_ASSERT_EQ_INT(10, (int)l.items[0].task);
    TEST_ASSERT_EQ_INT(12, (int)l.items[1].task);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_timer_cancel(&l, 1)); /* head */
    TEST_ASSERT_EQ_INT(1, (int)l.count);
    TEST_ASSERT_EQ_INT(12, (int)l.items[0].task);

    /* ============ capacity ============ */
    TEST_NAME("timer-capacity");
    mrt_timer_list_init(&l);
    uint32_t h = 0;
    for (uint32_t i = 0; i < MRT_MAX_TIMERS; i++) {
        TEST_ASSERT_EQ_INT(MRT_OK,
            mrt_timer_add(&l, (mrt_time_t)(1000 + i), 1, MRT_TIMER_TAG_WAKE, &h));
    }
    TEST_ASSERT_EQ_INT(MRT_ERR_FULL, mrt_timer_add(&l, 5, 2, 0, &h));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_timer_add(NULL, 5, 2, 0, &h));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_timer_add(&l, 5, 2, 0, NULL));

    /* ============ cancel_all_for ============ */
    TEST_NAME("timer-cancel-all-for");
    mrt_timer_list_init(&l);
    mrt_timer_add(&l, 5, 10, MRT_TIMER_TAG_WAKE, &h1);
    mrt_timer_add(&l, 6, 11, MRT_TIMER_TAG_IO, &h2);
    mrt_timer_add(&l, 7, 10, MRT_TIMER_TAG_WAKE, &h3);
    mrt_timer_add(&l, 8, 12, MRT_TIMER_TAG_DEADLINE, &h4);
    mrt_timer_cancel_all_for(&l, 10); /* removes both task-10 timers */
    TEST_ASSERT_EQ_INT(2, (int)l.count);
    TEST_ASSERT_EQ_INT(11, (int)l.items[0].task);
    TEST_ASSERT_EQ_INT(12, (int)l.items[1].task);
    mrt_timer_cancel_all_for(NULL, 10);   /* must not crash */
    mrt_timer_cancel_all_for(&l, MRT_TASK_ID_NONE); /* no-op */

    /* pop on NULL / out NULL */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_timer_pop_expired(&l, 5, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_timer_pop_expired(NULL, 5, &out));

    return TEST_SUMMARY();
}
