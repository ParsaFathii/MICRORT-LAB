/* mrt_types.h — MicroRT-Lab C kernel: base types.
 *
 * Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
 * Copyright © 2026 Parsa Fathi. Apache-2.0 license (see repository LICENSE).
 *
 * Design notes:
 *  - The kernel layer is pure C11, fixed-capacity, allocation-free at runtime.
 *    All capacities are compile-time constants so behaviour is deterministic
 *    and predictable, the way a small RTOS would be laid out.
 *  - Simulation time is an abstract integer tick count. It never depends on
 *    wall-clock time.
 *  - Higher numeric priority value means more urgent task (prio 10 beats 5).
 */
#ifndef MICRORT_MRT_TYPES_H
#define MICRORT_MRT_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Capacities                                                          */
/* ------------------------------------------------------------------ */
#define MRT_MAX_TASKS        64u   /* max registered tasks              */
#define MRT_MAX_RESOURCES    32u   /* mutexes+sems+msgq+evflags         */
#define MRT_MAX_WAITERS      32u   /* max tasks blocked on one resource */
#define MRT_MAX_TIMERS       128u  /* outstanding kernel timers         */
#define MRT_MAX_NAME         32u   /* max name length incl. NUL         */
#define MRT_READYQ_CAPACITY  (MRT_MAX_TASKS)
#define MRT_PRIO_MIN         (-100)
#define MRT_PRIO_MAX         100

/* ------------------------------------------------------------------ */
/* Core scalars                                                        */
/* ------------------------------------------------------------------ */
typedef uint64_t mrt_time_t;          /* simulation ticks, monotone    */
#define MRT_TIME_MAX UINT64_MAX

typedef int32_t mrt_prio_t;           /* higher value = more urgent    */
typedef uint16_t mrt_task_id_t;       /* kernel task handle            */
#define MRT_TASK_ID_NONE ((mrt_task_id_t)0xFFFFu)

typedef uint64_t mrt_seq_t;           /* monotone ordering counter     */

/* ------------------------------------------------------------------ */
/* Task state machine                                                  */
/*                                                                     */
/*  READY     runnable, waiting for CPU                                */
/*  RUNNING   occupying a CPU                                          */
/*  BLOCKED   waiting on a resource (mutex/sem/msgq/event flags)       */
/*  WAITING   waiting on a device (I/O burst in flight)                */
/*  SLEEPING  explicit timed sleep                                     */
/*  TERMINATED job finished / task ended                               */
/* ------------------------------------------------------------------ */
typedef enum mrt_task_state {
    MRT_TASK_UNUSED = 0,
    MRT_TASK_READY,
    MRT_TASK_RUNNING,
    MRT_TASK_BLOCKED,
    MRT_TASK_WAITING,
    MRT_TASK_SLEEPING,
    MRT_TASK_TERMINATED,
    MRT_TASK_STATE_COUNT
} mrt_task_state_t;

/* ------------------------------------------------------------------ */
/* Result codes                                                        */
/* ------------------------------------------------------------------ */
typedef enum mrt_result {
    MRT_OK = 0,
    MRT_ERR_INVALID_ARG = 1,     /* NULL pointer or bad handle        */
    MRT_ERR_FULL = 2,            /* fixed capacity exhausted          */
    MRT_ERR_NOT_FOUND = 3,       /* unknown id                        */
    MRT_ERR_BAD_STATE = 4,       /* illegal task state transition     */
    MRT_ERR_NOT_OWNER = 5,       /* unlock by non-owner               */
    MRT_ERR_OVERFLOW = 6,        /* semaphore at maximum count        */
    MRT_ERR_NO_MEMORY = 7,       /* allocator cannot satisfy request  */
    MRT_ERR_DUPLICATE = 8        /* duplicate registration            */
} mrt_result_t;

/* Kernel-visible task classification (mirrors experiment schema). */
typedef enum mrt_task_kind {
    MRT_TASK_APERIODIC = 0,   /* one job, released once at arrival    */
    MRT_TASK_PERIODIC  = 1,   /* job released every `period` ticks    */
    MRT_TASK_SPORADIC  = 2    /* jobs released at explicit instants   */
} mrt_task_kind_t;

/* Resource kinds. */
typedef enum mrt_res_kind {
    MRT_RES_MUTEX = 0,
    MRT_RES_SEMAPHORE = 1,
    MRT_RES_MSGQ = 2,
    MRT_RES_EVENTFLAGS = 3
} mrt_res_kind_t;

/* Mutex protocols. */
typedef enum mrt_mutex_proto {
    MRT_PROTO_NONE = 0,        /* plain priority-aware mutex           */
    MRT_PROTO_INHERIT = 1      /* priority inheritance on blocking     */
} mrt_mutex_proto_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_TYPES_H */
