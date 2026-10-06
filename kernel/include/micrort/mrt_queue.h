/* mrt_queue.h — MicroRT-Lab C kernel: scheduler-ready queue.
 *
 * A single ordered ring buffer. Ordering key:
 *   1. effective_priority DESC (higher value first)
 *   2. enqueue_seq ASC (FIFO within equal priority)
 * Insertion is O(n) ordered insert; n is bounded by MRT_MAX_TASKS so the
 * cost is constant-bounded and the order is fully deterministic.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_QUEUE_H
#define MICRORT_MRT_QUEUE_H

#include "mrt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mrt_readyq {
    mrt_task_id_t items[MRT_READYQ_CAPACITY];
    uint32_t      head;        /* pop side                            */
    uint32_t      count;       /* current occupancy                   */
} mrt_readyq_t;

void mrt_readyq_init(mrt_readyq_t *q);

/* Returns 1 when the queue must be re-ordered after a priority change.
 * Push assigns enqueue_seq via the kernel's monotone counter. */
mrt_result_t mrt_readyq_push(void *kernel, mrt_task_id_t id);

/* Pop highest priority task; MRT_TASK_ID_NONE if empty. */
mrt_task_id_t mrt_readyq_pop(void *kernel);

/* Peek without removing. */
mrt_task_id_t mrt_readyq_peek(const void *kernel);

/* Remove a specific task (e.g. blocked while ready — defensive path). */
mrt_result_t mrt_readyq_remove(void *kernel, mrt_task_id_t id);

/* Re-sort after effective priority changed (stable w.r.t. seq). */
mrt_result_t mrt_readyq_reposition(void *kernel, mrt_task_id_t id);

/* i-th element in dispatch order (0 = next). MRT_TASK_ID_NONE if oob. */
mrt_task_id_t mrt_readyq_at(const void *kernel, uint32_t index);

uint32_t mrt_readyq_size(const void *kernel);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_QUEUE_H */
