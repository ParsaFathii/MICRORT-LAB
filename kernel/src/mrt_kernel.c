/* mrt_kernel.c — kernel object initialization and counter helpers.
 *
 * The kernel structure is a plain zero-able value: mrt_kernel_init() is
 * memset(0) + next_seq = 1 (seq 0 is reserved as "never enqueued").
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "micrort/mrt_kernel.h"

#include <string.h>

void mrt_kernel_init(mrt_kernel_t *k)
{
    if (k == NULL) {
        return;
    }
    memset(k, 0, sizeof *k);
    k->next_seq = 1u; /* enqueue_seq 0 means "never queued" */
}

void mrt_kernel_note_ctx_switch(mrt_kernel_t *k)
{
    if (k != NULL) {
        k->ctx_switches++;
    }
}

void mrt_kernel_note_preemption(mrt_kernel_t *k)
{
    if (k != NULL) {
        k->preemptions++;
    }
}

void mrt_kernel_note_cpu_time(mrt_kernel_t *k, mrt_time_t ticks)
{
    if (k != NULL) {
        k->cpu_busy_time += ticks;
    }
}

void mrt_kernel_note_ctx_overhead(mrt_kernel_t *k, mrt_time_t ticks)
{
    if (k != NULL) {
        k->ctx_overhead_time += ticks;
    }
}

void mrt_kernel_note_idle(mrt_kernel_t *k, mrt_time_t ticks)
{
    if (k != NULL) {
        k->idle_time += ticks;
    }
}

uint32_t mrt_kernel_resource_count(const mrt_kernel_t *k)
{
    return (k == NULL) ? 0u : k->resource_count;
}

uint32_t mrt_kernel_task_count(const mrt_kernel_t *k)
{
    return (k == NULL) ? 0u : k->task_count;
}
