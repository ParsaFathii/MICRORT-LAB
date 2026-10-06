/* mrt_kernel.h — MicroRT-Lab C kernel: kernel object & umbrella include.
 *
 * One `mrt_kernel_t` instance represents the simulated machine's kernel
 * state: task table, ready queue, resources, timers, memory models and
 * global counters. The engine owns exactly one instance per simulation
 * run. All calls take `mrt_kernel_t*` (APIs above use void* to avoid
 * include cycles).
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_KERNEL_H
#define MICRORT_MRT_KERNEL_H

#include "mrt_types.h"
#include "mrt_task.h"
#include "mrt_queue.h"
#include "mrt_sync.h"
#include "mrt_mem.h"
#include "mrt_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mrt_kernel {
    /* task table */
    mrt_task_t    tasks[MRT_MAX_TASKS];
    uint32_t      task_count;

    /* dispatch queue */
    mrt_readyq_t  ready;

    /* resources */
    mrt_resource_t resources[MRT_MAX_RESOURCES];
    uint32_t       resource_count;

    /* delayed wake-ups / deadlines */
    mrt_timer_list_t timers;

    /* memory models (engine selects which one is "live") */
    mrt_mem_t  region;     /* variable-size model                     */
    mrt_pool_t pool;      /* fixed-size model                        */

    /* global counters */
    uint64_t   ctx_switches;
    uint64_t   preemptions;
    mrt_time_t cpu_busy_time;   /* ticks with a task RUNNING          */
    mrt_time_t ctx_overhead_time; /* ticks burned in context switches  */
    mrt_time_t idle_time;

    /* [Task 2-a amendment — documented in worklog.md] Current simulation
     * instant. The ENGINE advances this field at every event before calling
     * into the kernel. Synchronization calls that block or wake tasks
     * (mrt_mutex_lock/unlock, sem, msgq, evflags) take no `now` parameter,
     * yet must perform validated state transitions with correct time
     * accounting — they stamp those transitions with k->now. If the engine
     * never touches this field it stays 0 and sync-related accounting
     * reads 0 (graceful degradation, never a crash). */
    mrt_time_t now;

    /* monotone FIFO sequence for enqueue tie-breaks */
    mrt_seq_t  next_seq;

    /* aging configuration (engine sets; 0 disables) */
    mrt_time_t aging_interval;   /* every N ticks READY => +1 prio     */
    mrt_prio_t aging_cap;        /* max boost above base               */
} mrt_kernel_t;

/* Zero-initializes the kernel and registers the idle accounting
 * baseline. */
void mrt_kernel_init(mrt_kernel_t *k);

/* Bump helpers for counters (engine calls at the right moments). */
void mrt_kernel_note_ctx_switch(mrt_kernel_t *k);
void mrt_kernel_note_preemption(mrt_kernel_t *k);
void mrt_kernel_note_cpu_time(mrt_kernel_t *k, mrt_time_t ticks);
void mrt_kernel_note_ctx_overhead(mrt_kernel_t *k, mrt_time_t ticks);
void mrt_kernel_note_idle(mrt_kernel_t *k, mrt_time_t ticks);

/* Iterate live resources (index < resource_count, engine order). */
uint32_t mrt_kernel_resource_count(const mrt_kernel_t *k);
uint32_t mrt_kernel_task_count(const mrt_kernel_t *k);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_KERNEL_H */
