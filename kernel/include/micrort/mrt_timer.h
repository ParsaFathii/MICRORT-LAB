/* mrt_timer.h — MicroRT-Lab C kernel: one-shot timers.
 *
 * A delta-independent absolute-time ordered timer list. The engine uses
 * it for SLEEP expiry, I/O completion instants and per-job deadline
 * markers. Timers carry a task handle plus an opaque engine tag so the
 * engine can distinguish wake reasons.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_TIMER_H
#define MICRORT_MRT_TIMER_H

#include "mrt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t mrt_timer_tag_t;
/* Engine wake-reason tags (engine-private meaning; kernel treats them
 * as opaque values, these constants are shared for readability). */
#define MRT_TIMER_TAG_WAKE      1u   /* generic delayed wake          */
#define MRT_TIMER_TAG_IO        2u   /* I/O burst completion          */
#define MRT_TIMER_TAG_DEADLINE  3u   /* job deadline marker           */
#define MRT_TIMER_TAG_AGING     4u   /* aging recompute tick          */

typedef struct mrt_timer {
    mrt_time_t      fire_at;
    mrt_task_id_t   task;          /* MRT_TASK_ID_NONE = kernel timer */
    mrt_timer_tag_t tag;
    uint32_t        active;
} mrt_timer_t;

typedef struct mrt_timer_list {
    mrt_timer_t items[MRT_MAX_TIMERS];
    uint32_t    count;             /* compacted array, sorted ASC      */
} mrt_timer_list_t;

void mrt_timer_list_init(mrt_timer_list_t *l);

/* Insert keeping ascending fire_at order (stable: existing equal-time
 * timers stay before newly inserted ones). */
mrt_result_t mrt_timer_add(mrt_timer_list_t *l, mrt_time_t fire_at,
                           mrt_task_id_t task, mrt_timer_tag_t tag,
                           uint32_t *out_handle);

/* Remove by handle. */
mrt_result_t mrt_timer_cancel(mrt_timer_list_t *l, uint32_t handle);

/* Earliest expiry (MRT_TIME_MAX when empty). */
mrt_time_t mrt_timer_next(const mrt_timer_list_t *l);

/* Pop the timer at head if fire_at <= now; returns MRT_OK and fills
 * *out_timer, MRT_ERR_NOT_FOUND when nothing is due yet. */
mrt_result_t mrt_timer_pop_expired(mrt_timer_list_t *l, mrt_time_t now,
                                   mrt_timer_t *out_timer);

/* Cancel every active timer belonging to `task` (on job completion). */
void mrt_timer_cancel_all_for(mrt_timer_list_t *l, mrt_task_id_t task);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_TIMER_H */
