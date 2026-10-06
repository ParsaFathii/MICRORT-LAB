/* mrt_queue.c — scheduler-ready queue (ordered ring buffer).
 *
 * Layout: fixed ring of MRT_READYQ_CAPACITY slots. Logical element i lives
 * at physical index (head + i) % CAP. Ordering key:
 *      1. effective_priority DESC
 *      2. enqueue_seq ASC (FIFO within equal priority)
 *
 * mrt_readyq_push stamps the task with a FRESH seq from kernel->next_seq
 * (monotone, starts at 1 — 0 means "never enqueued"), so a freshly pushed
 * task always sorts after equal-priority residents (FIFO). reposition
 * re-inserts with the task's EXISTING seq, preserving its FIFO place.
 *
 * MRT_ERR_FULL is unreachable through legal use (capacity == MRT_MAX_TASKS
 * and ids are unique, double-push is rejected with MRT_ERR_DUPLICATE); the
 * check exists for defensiveness only.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "mrt_internal.h"

void mrt_readyq_init(mrt_readyq_t *q)
{
    if (q == NULL) {
        return;
    }
    q->head = 0u;
    q->count = 0u;
    for (uint32_t i = 0u; i < MRT_READYQ_CAPACITY; i++) {
        q->items[i] = MRT_TASK_ID_NONE;
    }
}

static uint32_t q_phys(const mrt_readyq_t *q, uint32_t i)
{
    return (q->head + i) % MRT_READYQ_CAPACITY;
}

static int q_contains(const mrt_kernel_t *k, mrt_task_id_t id)
{
    const mrt_readyq_t *q = &k->ready;
    for (uint32_t i = 0u; i < q->count; i++) {
        if (q->items[q_phys(q, i)] == id) {
            return 1;
        }
    }
    return 0;
}

/* Ordered insert using the task's CURRENT enqueue_seq field. */
static void q_insert(mrt_kernel_t *k, mrt_task_id_t id)
{
    mrt_readyq_t *q = &k->ready;
    mrt_task_t *t = mrt_task_ref(k, id);
    if (t == NULL) {
        return; /* unreachable for validated ids */
    }
    uint32_t pos = q->count;
    for (uint32_t i = 0u; i < q->count; i++) {
        mrt_task_t *o = mrt_task_ref(k, q->items[q_phys(q, i)]);
        if (o == NULL) {
            continue;
        }
        if ((o->effective_priority < t->effective_priority) ||
            ((o->effective_priority == t->effective_priority) &&
             (o->enqueue_seq > t->enqueue_seq))) {
            pos = i;
            break;
        }
    }
    for (uint32_t j = q->count; j > pos; j--) {
        q->items[q_phys(q, j)] = q->items[q_phys(q, j - 1u)];
    }
    q->items[q_phys(q, pos)] = id;
    q->count++;
}

static void q_erase(mrt_kernel_t *k, mrt_task_id_t id)
{
    mrt_readyq_t *q = &k->ready;
    for (uint32_t i = 0u; i < q->count; i++) {
        if (q->items[q_phys(q, i)] == id) {
            for (uint32_t j = i; (j + 1u) < q->count; j++) {
                q->items[q_phys(q, j)] = q->items[q_phys(q, j + 1u)];
            }
            q->items[q_phys(q, q->count - 1u)] = MRT_TASK_ID_NONE;
            q->count--;
            return;
        }
    }
}

mrt_result_t mrt_readyq_push(void *kernel, mrt_task_id_t id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_task_t *t = mrt_task_ref(k, id);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    if (q_contains(k, id)) {
        return MRT_ERR_DUPLICATE; /* defensive: no double-queue */
    }
    if (k->ready.count >= MRT_READYQ_CAPACITY) {
        return MRT_ERR_FULL;
    }
    t->enqueue_seq = k->next_seq++; /* fresh FIFO stamp */
    q_insert(k, id);
    return MRT_OK;
}

mrt_task_id_t mrt_readyq_pop(void *kernel)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || k->ready.count == 0u) {
        return MRT_TASK_ID_NONE;
    }
    mrt_readyq_t *q = &k->ready;
    mrt_task_id_t id = q->items[q->head];
    q->items[q->head] = MRT_TASK_ID_NONE;
    q->head = (q->head + 1u) % MRT_READYQ_CAPACITY;
    q->count--;
    return id;
}

mrt_task_id_t mrt_readyq_peek(const void *kernel)
{
    const mrt_kernel_t *k = (const mrt_kernel_t *)kernel;
    if (k == NULL || k->ready.count == 0u) {
        return MRT_TASK_ID_NONE;
    }
    return k->ready.items[k->ready.head];
}

mrt_task_id_t mrt_readyq_at(const void *kernel, uint32_t index)
{
    const mrt_kernel_t *k = (const mrt_kernel_t *)kernel;
    if (k == NULL || index >= k->ready.count) {
        return MRT_TASK_ID_NONE;
    }
    return k->ready.items[(k->ready.head + index) % MRT_READYQ_CAPACITY];
}

uint32_t mrt_readyq_size(const void *kernel)
{
    const mrt_kernel_t *k = (const mrt_kernel_t *)kernel;
    return (k == NULL) ? 0u : k->ready.count;
}

mrt_result_t mrt_readyq_remove(void *kernel, mrt_task_id_t id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (!q_contains(k, id)) {
        return MRT_ERR_NOT_FOUND;
    }
    q_erase(k, id);
    return MRT_OK;
}

mrt_result_t mrt_readyq_reposition(void *kernel, mrt_task_id_t id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (!q_contains(k, id)) {
        return MRT_ERR_NOT_FOUND;
    }
    q_erase(k, id);
    q_insert(k, id); /* keeps the task's existing enqueue_seq */
    return MRT_OK;
}
