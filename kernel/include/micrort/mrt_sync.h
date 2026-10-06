/* mrt_sync.h — MicroRT-Lab C kernel: synchronization primitives.
 *
 * Mutex, counting semaphore, message queue and event flags. All blocking
 * decisions are recorded as kernel state changes; the simulation engine
 * decides WHEN to call these (interpreting task programs) and turns the
 * state changes into trace events.
 *
 * Wake-up policy (documented, deterministic):
 *   - mutex / semaphore: highest effective priority waiter (FIFO ties)
 *   - message queue:     FIFO waiter order (preserves data order)
 *   - event flags:       all satisfied waiters woken simultaneously
 *
 * Mutex hand-off: on unlock, if waiters exist the highest-priority waiter
 * becomes the new owner and is moved to READY (no free-then-relock race
 * is modelled). With protocol INHERIT, blocking boosts the owner and
 * unlock/recompute restores it (see mrt_task_recompute_priority).
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_SYNC_H
#define MICRORT_MRT_SYNC_H

#include "mrt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mrt_resource {
    char           id[MRT_MAX_NAME];
    mrt_res_kind_t kind;

    /* --- mutex --- */
    mrt_task_id_t   owner;             /* MRT_TASK_ID_NONE when free   */
    mrt_mutex_proto_t protocol;        /* NONE or INHERIT              */

    /* --- counting semaphore --- */
    int32_t         sem_count;
    int32_t         sem_max;

    /* --- message queue (integer payloads) --- */
    int32_t         msg_capacity;
    int32_t         msg_head;           /* dequeue index                */
    int32_t         msg_count;
    int32_t        *msg_buf;            /* engine-provided backing      */

    /* --- event flags --- */
    uint32_t        evflags;

    /* --- waiter list, ordered per wake-up policy --- */
    mrt_task_id_t   waiters[MRT_MAX_WAITERS];
    uint32_t        waiter_count;

    /* --- statistics --- */
    uint32_t        acquisitions;       /* successful ops               */
    uint32_t        contentions;        /* ops that had to block        */
} mrt_resource_t;

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */
mrt_result_t mrt_res_register_mutex(void *kernel, const char *id,
                                    mrt_mutex_proto_t protocol,
                                    uint32_t *out_index);
mrt_result_t mrt_res_register_sem(void *kernel, const char *id,
                                  int32_t initial_count, int32_t max_count,
                                  uint32_t *out_index);
/* msg_buf must point to an int32 array of length capacity, owned by the
 * engine (kernel never frees it). */
mrt_result_t mrt_res_register_msgq(void *kernel, const char *id,
                                   int32_t capacity, int32_t *msg_buf,
                                   uint32_t *out_index);
mrt_result_t mrt_res_register_evflags(void *kernel, const char *id,
                                      uint32_t *out_index);

/* Look up by string id. Returns MRT_ERR_NOT_FOUND when absent. */
mrt_result_t mrt_res_find(void *kernel, const char *id, uint32_t *out_index);
mrt_resource_t *mrt_res_get(void *kernel, uint32_t index);

/* ------------------------------------------------------------------ */
/* Mutex                                                               */
/* ------------------------------------------------------------------ */
/* Attempts a lock for task `task`.
 *   *acquired = 1 -> task owns the mutex now (state unchanged)
 *   *acquired = 0 -> task was appended to the waiters and its state set
 *                    to MRT_TASK_BLOCKED (caller records the trace).
 * If protocol = INHERIT and the call blocks, the owner's effective
 * priority is raised and the ready queue repositioned. */
mrt_result_t mrt_mutex_lock(void *kernel, uint32_t res_index,
                            mrt_task_id_t task, int *acquired);

/* Unlock. If waiters exist the mutex is handed to the next owner and
 * that task is moved to READY; *handed_to receives its id (or NONE).
 * Priority inheritance is rolled back via priority recompute. */
mrt_result_t mrt_mutex_unlock(void *kernel, uint32_t res_index,
                              mrt_task_id_t task,
                              mrt_task_id_t *handed_to);

/* Waiter that would receive the mutex next (diagnostics / RAG edges). */
mrt_task_id_t mrt_mutex_next_waiter(const void *kernel, uint32_t res_index);

/* ------------------------------------------------------------------ */
/* Counting semaphore                                                  */
/* ------------------------------------------------------------------ */
/* wait: *acquired=1 when count>0 (count decremented); otherwise the
 * task blocks (state -> BLOCKED, added to waiters). */
mrt_result_t mrt_sem_wait(void *kernel, uint32_t res_index,
                          mrt_task_id_t task, int *acquired);

/* signal: when waiters exist the token is handed directly to the
 * highest-priority waiter (*woken, moved to READY, count unchanged);
 * otherwise count++ (MRT_ERR_OVERFLOW when already at sem_max — the
 * count stays clamped and the caller records the anomaly). */
mrt_result_t mrt_sem_signal(void *kernel, uint32_t res_index,
                            mrt_task_id_t *woken);

/* ------------------------------------------------------------------ */
/* Message queue                                                       */
/* ------------------------------------------------------------------ */
/* send: *sent=1 when space available (payload enqueued); otherwise the
 * sending task blocks (BLOCKED). On success, if receivers are waiting,
 * the FIFO-first receiver is woken (*woken) — it will dequeue on resume;
 * the payload stays queued so ordering is preserved. */
mrt_result_t mrt_msgq_send(void *kernel, uint32_t res_index,
                           mrt_task_id_t task, int32_t payload,
                           int *sent, mrt_task_id_t *woken);

/* recv: *value receives the dequeued payload; when empty the task
 * blocks. */
mrt_result_t mrt_msgq_recv(void *kernel, uint32_t res_index,
                           mrt_task_id_t task, int32_t *value,
                           int *received);

/* Peek current payload for a receiver resuming after wake (engine use). */
mrt_result_t mrt_msgq_peek(const void *kernel, uint32_t res_index,
                           int32_t *value);

/* ------------------------------------------------------------------ */
/* Event flags                                                         */
/* ------------------------------------------------------------------ */
/* task waits for (flags & mask):
 *   mode 0 = ANY bit, mode 1 = ALL bits.
 * *satisfied=1 when the condition already holds; otherwise the task
 * blocks (BLOCKED). */
mrt_result_t mrt_evflags_wait(void *kernel, uint32_t res_index,
                              mrt_task_id_t task, uint32_t mask,
                              int all_bits, int *satisfied);

/* Sets bits (OR). Wakes every waiter whose condition now holds
 * (*woken_count receives how many, woken[] their ids, cap woken_cap).
 * Woken tasks move to READY. */
mrt_result_t mrt_evflags_set(void *kernel, uint32_t res_index,
                             uint32_t mask, mrt_task_id_t *woken,
                             uint32_t woken_cap, uint32_t *woken_count);

/* Clears selected bits (engine diagnostics; task program op). */
mrt_result_t mrt_evflags_clear(void *kernel, uint32_t res_index,
                               uint32_t mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_SYNC_H */
