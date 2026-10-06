/* mrt_task.h — MicroRT-Lab C kernel: task control block & lifecycle.
 *
 * The kernel owns the task table. The C++ engine registers tasks from the
 * experiment configuration and drives state transitions through the
 * validated API below. Direct field mutation from outside the kernel
 * implementation is discouraged (all accessors needed are provided).
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_TASK_H
#define MICRORT_MRT_TASK_H

#include "mrt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Task control block                                                  */
/* ------------------------------------------------------------------ */
typedef struct mrt_task {
    mrt_task_id_t  id;               /* stable kernel handle            */
    char           name[MRT_MAX_NAME];

    mrt_task_kind_t kind;            /* aperiodic / periodic / sporadic */
    mrt_prio_t     base_priority;    /* configured priority             */
    mrt_prio_t     effective_priority; /* base or boosted (inherit/age) */

    mrt_task_state_t state;

    mrt_time_t     arrival;          /* configured first release        */
    mrt_time_t     period;           /* periodic only                   */
    mrt_time_t     relative_deadline;/* 0 = no deadline                 */

    /* ---- cumulative statistics (updated by kernel calls) ---- */
    mrt_time_t     cpu_time_total;   /* ticks spent RUNNING             */
    mrt_time_t     ready_wait_total; /* READY residence time            */
    mrt_time_t     blocked_total;    /* BLOCKED time (resource waits)   */
    mrt_time_t     waiting_total;    /* WAITING time (I/O)              */
    mrt_time_t     sleeping_total;   /* SLEEPING time                   */
    mrt_time_t     last_ready_at;    /* when it entered READY last      */
    mrt_time_t     first_dispatch;   /* 0 + dispatched_once flag        */
    uint8_t        dispatched_once;

    uint32_t       jobs_released;
    uint32_t       jobs_completed;
    uint32_t       deadline_misses;

    mrt_seq_t      enqueue_seq;      /* FIFO tie-break within a prio    */
    uint32_t       aging_units;      /* aging boosts applied (see cfg)  */

    /* engine-private extension block (opaque to kernel logic)         */
    uint64_t       engine_ctx;       /* engine may store an index here  */
} mrt_task_t;

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */
/* Registers a task and returns its handle. Name is copied (truncated at
 * MRT_MAX_NAME-1). The task starts in state MRT_TASK_UNUSED until
 * released via mrt_task_release(). Returns:
 *   MRT_OK, MRT_ERR_FULL, MRT_ERR_INVALID_ARG. */
mrt_result_t mrt_task_register(void            *kernel,
                               const char      *name,
                               mrt_task_kind_t  kind,
                               mrt_prio_t       base_priority,
                               mrt_time_t       arrival,
                               mrt_time_t       period,
                               mrt_time_t       relative_deadline,
                               mrt_task_id_t   *out_id);

/* Fetch a task by handle. NULL if unknown. */
mrt_task_t *mrt_task_get(void *kernel, mrt_task_id_t id);

/* Find a task by name. MRT_TASK_ID_NONE when absent. */
mrt_task_id_t mrt_task_find(void *kernel, const char *name);

/* ------------------------------------------------------------------ */
/* State machine                                                       */
/* ------------------------------------------------------------------ */
/* Validated transition. Allowed edges:
 *   UNUSED     -> READY              (release of a job)
 *   READY      -> RUNNING            (dispatch)
 *   RUNNING    -> READY              (preemption / quantum expiry)
 *   RUNNING    -> BLOCKED/WAITING/SLEEPING/TERMINATED
 *   BLOCKED    -> READY              (resource granted)
 *   WAITING    -> READY              (I/O completed)
 *   SLEEPING   -> READY              (timer expired)
 * Any other edge returns MRT_ERR_BAD_STATE and changes nothing.
 * Accounting counters (ready_wait_total etc.) are updated here. */
mrt_result_t mrt_task_set_state(void *kernel, mrt_task_id_t id,
                                mrt_task_state_t new_state, mrt_time_t now);

/* ------------------------------------------------------------------ */
/* Priority handling                                                   */
/* ------------------------------------------------------------------ */
/* Recompute effective priority:
 *   effective = max(base, max effective priority of tasks blocked on
 *                   any mutex currently owned by this task whose
 *                   protocol is INHERIT) + aging_units.
 * The kernel repositions the task in the ready queue if queued. */
mrt_result_t mrt_task_recompute_priority(void *kernel, mrt_task_id_t id);

/* Explicit boost (used by engine for aging). Clamped to MRT_PRIO_MAX. */
mrt_result_t mrt_task_boost(void *kernel, mrt_task_id_t id, mrt_prio_t add);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_TASK_H */
