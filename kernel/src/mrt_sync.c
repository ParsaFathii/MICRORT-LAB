/* mrt_sync.c — mutex (hand-off + priority inheritance), counting
 * semaphore, message queue (FIFO wake) and event flags.
 *
 * Kernel-kept invariants (2-a):
 *  - ALL task state changes for resource waits happen HERE (via
 *    mrt_task_set_state), stamped with kernel->now (see mrt_kernel.h
 *    amendment). The engine only traces what these functions report.
 *  - Blocking requires the task to be RUNNING (the only legal entry into
 *    BLOCKED). If the engine violates that, mrt_task_set_state returns
 *    MRT_ERR_BAD_STATE and NOTHING is mutated (waiters list untouched).
 *  - A woken task is moved to READY but NOT pushed into the ready queue:
 *    queue membership is the engine's orchestration (push on ready, pop on
 *    dispatch). This mirrors mrt_task_set_state, which never touches the
 *    queue either.
 *
 * Waiter list orderings (deterministic, documented):
 *  - mutex / semaphore: (effective_priority DESC, enqueue_seq ASC); the
 *    lists are re-sorted whenever a waiter's priority changes.
 *  - message queue:     FIFO arrival order (data order must match arrival
 *    order; priority would reorder senders/receivers unfairly).
 *  - event flags:       FIFO arrival order; wake policy wakes ALL
 *    satisfied waiters, so the order only affects the report array.
 *
 * Priority inheritance (mutex, protocol INHERIT):
 *  - When a task blocks on an INHERIT mutex and its effective priority is
 *    higher than the owner's, the owner is immediately raised to the
 *    waiter's priority and the raise is propagated transitively along
 *    chains of INHERIT mutexes the raised task is itself blocked on.
 *    Raises are strictly monotone, so propagation terminates even in the
 *    presence of RAG cycles (the engine's deadlock detector reports those).
 *  - Rollback happens through mrt_task_recompute_priority, called for the
 *    unlocking task AND the new owner on every unlock.
 *  - Relock by the current owner returns MRT_ERR_BAD_STATE (a self-cycle
 *    in the RAG; the engine's deadlock detector owns that diagnosis).
 *
 * Event flags wait-parameter convention (2-a, documented):
 *  A task blocked on an EVENTFLAGS resource carries its wait parameters
 *  packed in task->engine_ctx (uint64): bits 0..31 = mask, bit 32 =
 *  all_bits flag. Bit 32 (not 31) is used so all 32 mask bits stay
 *  available. mrt_evflags_set unpacks engine_ctx to decide which waiters
 *  are satisfied. THE ENGINE MUST NOT USE engine_ctx FOR OTHER PURPOSES
 *  WHILE A TASK IS BLOCKED ON EVENT FLAGS; the packed value is left in
 *  place after wake so the engine can read what the task was waiting for.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "mrt_internal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* resource table                                                      */
/* ------------------------------------------------------------------ */

static mrt_resource_t *res_at(mrt_kernel_t *k, uint32_t index)
{
    if (k == NULL || index >= k->resource_count || index >= MRT_MAX_RESOURCES) {
        return NULL;
    }
    return &k->resources[index];
}

/* NULL / unknown-index / wrong-kind validation in one step. */
static mrt_result_t res_check(void *kernel, uint32_t index,
                              mrt_res_kind_t kind, mrt_resource_t **out)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = res_at(k, index);
    if (r == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    if (r->kind != kind) {
        return MRT_ERR_INVALID_ARG;
    }
    if (out != NULL) {
        *out = r;
    }
    return MRT_OK;
}

static void res_name_copy(char dst[MRT_MAX_NAME], const char *src)
{
    uint32_t i = 0u;
    while ((i + 1u < MRT_MAX_NAME) && (src[i] != '\0')) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static mrt_result_t res_register_common(void *kernel, const char *id,
                                        mrt_res_kind_t kind, uint32_t *out_index)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || id == NULL || out_index == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (k->resource_count >= MRT_MAX_RESOURCES) {
        return MRT_ERR_FULL;
    }
    for (uint32_t i = 0u; i < k->resource_count; i++) {
        if (strcmp(k->resources[i].id, id) == 0) {
            return MRT_ERR_DUPLICATE;
        }
    }
    mrt_resource_t *r = &k->resources[k->resource_count];
    memset(r, 0, sizeof *r);
    res_name_copy(r->id, id);
    r->kind = kind;
    r->owner = MRT_TASK_ID_NONE;
    *out_index = k->resource_count;
    k->resource_count++;
    return MRT_OK;
}

mrt_result_t mrt_res_register_mutex(void *kernel, const char *id,
                                    mrt_mutex_proto_t protocol, uint32_t *out_index)
{
    if (protocol != MRT_PROTO_NONE && protocol != MRT_PROTO_INHERIT) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_result_t rc = res_register_common(kernel, id, MRT_RES_MUTEX, out_index);
    if (rc != MRT_OK) {
        return rc;
    }
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    k->resources[*out_index].protocol = protocol;
    return MRT_OK;
}

mrt_result_t mrt_res_register_sem(void *kernel, const char *id,
                                  int32_t initial_count, int32_t max_count,
                                  uint32_t *out_index)
{
    if (max_count < 1 || initial_count < 0 || initial_count > max_count) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_result_t rc = res_register_common(kernel, id, MRT_RES_SEMAPHORE, out_index);
    if (rc != MRT_OK) {
        return rc;
    }
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    k->resources[*out_index].sem_count = initial_count;
    k->resources[*out_index].sem_max = max_count;
    return MRT_OK;
}

mrt_result_t mrt_res_register_msgq(void *kernel, const char *id,
                                   int32_t capacity, int32_t *msg_buf,
                                   uint32_t *out_index)
{
    if (capacity < 1 || msg_buf == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_result_t rc = res_register_common(kernel, id, MRT_RES_MSGQ, out_index);
    if (rc != MRT_OK) {
        return rc;
    }
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    k->resources[*out_index].msg_capacity = capacity;
    k->resources[*out_index].msg_buf = msg_buf;
    return MRT_OK;
}

mrt_result_t mrt_res_register_evflags(void *kernel, const char *id,
                                      uint32_t *out_index)
{
    return res_register_common(kernel, id, MRT_RES_EVENTFLAGS, out_index);
}

mrt_result_t mrt_res_find(void *kernel, const char *id, uint32_t *out_index)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || id == NULL || out_index == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    for (uint32_t i = 0u; i < k->resource_count; i++) {
        if (strcmp(k->resources[i].id, id) == 0) {
            *out_index = i;
            return MRT_OK;
        }
    }
    return MRT_ERR_NOT_FOUND;
}

mrt_resource_t *mrt_res_get(void *kernel, uint32_t index)
{
    return res_at((mrt_kernel_t *)kernel, index);
}

/* ------------------------------------------------------------------ */
/* waiter list helpers                                                 */
/* ------------------------------------------------------------------ */

static int wfind(const mrt_resource_t *r, mrt_task_id_t id)
{
    for (uint32_t i = 0u; i < r->waiter_count; i++) {
        if (r->waiters[i] == id) {
            return (int)i;
        }
    }
    return -1;
}

static void wremove_at(mrt_resource_t *r, uint32_t i)
{
    for (uint32_t j = i; (j + 1u) < r->waiter_count; j++) {
        r->waiters[j] = r->waiters[j + 1u];
    }
    r->waiter_count--;
}

static void wappend(mrt_resource_t *r, mrt_task_id_t id) /* FIFO insert */
{
    r->waiters[r->waiter_count] = id;
    r->waiter_count++;
}

/* priority-ordered insert: (effective_priority DESC, enqueue_seq ASC) */
static void winsert_prio(mrt_kernel_t *k, mrt_resource_t *r, mrt_task_id_t id)
{
    mrt_task_t *t = mrt_task_ref(k, id);
    if (t == NULL) {
        return; /* unreachable for validated ids */
    }
    uint32_t pos = r->waiter_count;
    for (uint32_t i = 0u; i < r->waiter_count; i++) {
        mrt_task_t *o = mrt_task_ref(k, r->waiters[i]);
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
    for (uint32_t j = r->waiter_count; j > pos; j--) {
        r->waiters[j] = r->waiters[j - 1u];
    }
    r->waiters[pos] = id;
    r->waiter_count++;
}

/* index of the best waiter by (effective_priority DESC, enqueue_seq ASC);
 * the lists are kept sorted, but scanning keeps hand-off correct even if
 * priorities changed without a resort. */
static uint32_t waiter_best(mrt_kernel_t *k, const mrt_resource_t *r)
{
    uint32_t best = 0u;
    const mrt_task_t *bt = mrt_task_ref(k, r->waiters[0]);
    for (uint32_t i = 1u; i < r->waiter_count; i++) {
        const mrt_task_t *o = mrt_task_ref(k, r->waiters[i]);
        if (o == NULL || bt == NULL) {
            continue;
        }
        if ((o->effective_priority > bt->effective_priority) ||
            ((o->effective_priority == bt->effective_priority) &&
             (o->enqueue_seq < bt->enqueue_seq))) {
            best = i;
            bt = o;
        }
    }
    return best;
}

void mrt_sync_resort_waiter_lists(void *kernel, mrt_task_id_t id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return;
    }
    for (uint32_t r = 0u; r < k->resource_count; r++) {
        mrt_resource_t *res = &k->resources[r];
        if (res->kind != MRT_RES_MUTEX && res->kind != MRT_RES_SEMAPHORE) {
            continue; /* msgq/evflags lists are FIFO — priority is not key */
        }
        int i = wfind(res, id);
        if (i < 0) {
            continue;
        }
        wremove_at(res, (uint32_t)i);
        winsert_prio(k, res, id);
    }
}

/* ------------------------------------------------------------------ */
/* priority inheritance propagation                                    */
/* ------------------------------------------------------------------ */

/* Raise `id` to `prio` (only if strictly lower) and propagate along every
 * INHERIT mutex the raised task is itself blocked on. Strict monotonicity
 * guarantees termination; worklist is bounded by MRT_MAX_TASKS entries. */
static void inherit_raise(mrt_kernel_t *k, mrt_task_id_t id, mrt_prio_t prio)
{
    mrt_task_id_t work[MRT_MAX_TASKS];
    mrt_prio_t target[MRT_MAX_TASKS];
    uint32_t wn = 0u;

    work[wn] = id;
    target[wn] = prio;
    wn++;

    while (wn > 0u) {
        wn--;
        mrt_task_id_t tid = work[wn];
        mrt_prio_t p = target[wn];
        mrt_task_t *t = mrt_task_ref(k, tid);
        if (t == NULL || t->effective_priority >= p) {
            continue; /* already at/above the target — stop this branch */
        }
        t->effective_priority = p;
        mrt_task_priority_changed(k, tid);

        /* the raised task may be blocked on other INHERIT mutexes whose
         * owners must inherit the raise as well (transitive PI) */
        for (uint32_t r = 0u; r < k->resource_count; r++) {
            mrt_resource_t *res = &k->resources[r];
            if (res->kind != MRT_RES_MUTEX || res->protocol != MRT_PROTO_INHERIT) {
                continue;
            }
            if (res->owner == MRT_TASK_ID_NONE || res->owner == tid) {
                continue;
            }
            if (wfind(res, tid) < 0) {
                continue;
            }
            if (wn < MRT_MAX_TASKS) {
                work[wn] = res->owner;
                target[wn] = p;
                wn++;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* mutex                                                               */
/* ------------------------------------------------------------------ */

mrt_result_t mrt_mutex_lock(void *kernel, uint32_t res_index,
                            mrt_task_id_t task, int *acquired)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || acquired == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_MUTEX, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    mrt_task_t *t = mrt_task_ref(kernel, task);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *acquired = 0;

    if (r->owner == task) {
        /* relock by the owner: rejected (RAG self-cycle — see header) */
        return MRT_ERR_BAD_STATE;
    }
    if (r->owner == MRT_TASK_ID_NONE) {
        r->owner = task;
        r->acquisitions++;
        *acquired = 1; /* state unchanged */
        return MRT_OK;
    }

    /* contended: block the caller (must currently be RUNNING) */
    if (wfind(r, task) >= 0) {
        return MRT_ERR_DUPLICATE; /* defensive: already blocked here */
    }
    if (r->waiter_count >= MRT_MAX_WAITERS) {
        return MRT_ERR_FULL;
    }
    rc = mrt_task_set_state(kernel, task, MRT_TASK_BLOCKED, k->now);
    if (rc != MRT_OK) {
        return rc; /* nothing mutated */
    }
    winsert_prio(k, r, task);
    r->contentions++;

    if (r->protocol == MRT_PROTO_INHERIT) {
        mrt_task_t *owner = mrt_task_ref(kernel, r->owner);
        if (owner != NULL && t->effective_priority > owner->effective_priority) {
            inherit_raise(k, r->owner, t->effective_priority);
        }
    }
    return MRT_OK;
}

mrt_result_t mrt_mutex_unlock(void *kernel, uint32_t res_index,
                              mrt_task_id_t task, mrt_task_id_t *handed_to)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || handed_to == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_MUTEX, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    if (mrt_task_ref(kernel, task) == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *handed_to = MRT_TASK_ID_NONE;
    if (r->owner != task) {
        return MRT_ERR_NOT_OWNER;
    }

    if (r->waiter_count > 0u) {
        /* hand-off: best waiter becomes the new owner immediately */
        uint32_t bi = waiter_best(k, r);
        mrt_task_id_t next = r->waiters[bi];
        rc = mrt_task_set_state(kernel, next, MRT_TASK_READY, k->now);
        if (rc != MRT_OK) {
            return rc; /* defensive: invariant makes this unreachable */
        }
        wremove_at(r, bi);
        r->owner = next;
        r->acquisitions++; /* the handed-off lock counts as acquired */
        *handed_to = next;
        /* new owner may hold other INHERIT mutexes / inherit from the
         * remaining waiters of this one */
        (void)mrt_task_recompute_priority(kernel, next);
    } else {
        r->owner = MRT_TASK_ID_NONE;
    }
    /* roll back any inheritance the unlocker had accumulated */
    (void)mrt_task_recompute_priority(kernel, task);
    return MRT_OK;
}

mrt_task_id_t mrt_mutex_next_waiter(const void *kernel, uint32_t res_index)
{
    const mrt_kernel_t *k = (const mrt_kernel_t *)kernel;
    if (k == NULL || res_index >= k->resource_count) {
        return MRT_TASK_ID_NONE;
    }
    const mrt_resource_t *r = &k->resources[res_index];
    if (r->kind != MRT_RES_MUTEX || r->waiter_count == 0u) {
        return MRT_TASK_ID_NONE;
    }
    mrt_kernel_t *nk = (mrt_kernel_t *)(uintptr_t)k; /* read-only usage */
    return r->waiters[waiter_best(nk, r)];
}

/* ------------------------------------------------------------------ */
/* counting semaphore                                                  */
/* ------------------------------------------------------------------ */

mrt_result_t mrt_sem_wait(void *kernel, uint32_t res_index,
                          mrt_task_id_t task, int *acquired)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || acquired == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_SEMAPHORE, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    if (mrt_task_ref(kernel, task) == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *acquired = 0;

    if (r->sem_count > 0) {
        r->sem_count--;
        r->acquisitions++;
        *acquired = 1; /* state unchanged */
        return MRT_OK;
    }

    /* no token: block (task must be RUNNING) */
    if (wfind(r, task) >= 0) {
        return MRT_ERR_DUPLICATE; /* defensive */
    }
    if (r->waiter_count >= MRT_MAX_WAITERS) {
        return MRT_ERR_FULL;
    }
    rc = mrt_task_set_state(kernel, task, MRT_TASK_BLOCKED, k->now);
    if (rc != MRT_OK) {
        return rc;
    }
    winsert_prio(k, r, task);
    r->contentions++;
    return MRT_OK;
}

mrt_result_t mrt_sem_signal(void *kernel, uint32_t res_index,
                            mrt_task_id_t *woken)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || woken == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_SEMAPHORE, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    *woken = MRT_TASK_ID_NONE;

    if (r->waiter_count > 0u) {
        /* token handed directly: highest-priority waiter, count unchanged */
        uint32_t bi = waiter_best(k, r);
        mrt_task_id_t id = r->waiters[bi];
        rc = mrt_task_set_state(kernel, id, MRT_TASK_READY, k->now);
        if (rc != MRT_OK) {
            return rc; /* defensive */
        }
        wremove_at(r, bi);
        r->acquisitions++; /* the woken task's wait is thereby satisfied */
        *woken = id;
        return MRT_OK;
    }
    if (r->sem_count < r->sem_max) {
        r->sem_count++;
        return MRT_OK;
    }
    return MRT_ERR_OVERFLOW; /* count stays clamped at sem_max */
}

/* ------------------------------------------------------------------ */
/* message queue                                                       */
/* ------------------------------------------------------------------ */

mrt_result_t mrt_msgq_send(void *kernel, uint32_t res_index,
                           mrt_task_id_t task, int32_t payload,
                           int *sent, mrt_task_id_t *woken)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || sent == NULL || woken == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_MSGQ, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    if (mrt_task_ref(kernel, task) == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *sent = 0;
    *woken = MRT_TASK_ID_NONE;

    if (r->msg_count < r->msg_capacity) {
        /* space available: enqueue at the tail of the ring */
        uint32_t slot = ((uint32_t)r->msg_head + (uint32_t)r->msg_count) %
                        (uint32_t)r->msg_capacity;
        r->msg_buf[slot] = payload;
        r->msg_count++;
        r->acquisitions++;
        *sent = 1;

        if (r->waiter_count > 0u) {
            /* FIFO-first receiver is woken; the payload STAYS queued — the
             * receiver re-attempts recv when dispatched. */
            mrt_task_id_t id = r->waiters[0];
            rc = mrt_task_set_state(kernel, id, MRT_TASK_READY, k->now);
            if (rc != MRT_OK) {
                return rc; /* defensive */
            }
            wremove_at(r, 0u);
            *woken = id;
        }
        return MRT_OK;
    }

    /* full: sender blocks (waiters are FIFO-ordered for msgq — see header
     * of this file; priority order would corrupt data-order fairness) */
    if (wfind(r, task) >= 0) {
        return MRT_ERR_DUPLICATE; /* defensive */
    }
    if (r->waiter_count >= MRT_MAX_WAITERS) {
        return MRT_ERR_FULL;
    }
    rc = mrt_task_set_state(kernel, task, MRT_TASK_BLOCKED, k->now);
    if (rc != MRT_OK) {
        return rc;
    }
    wappend(r, task);
    r->contentions++;
    return MRT_OK;
}

mrt_result_t mrt_msgq_recv(void *kernel, uint32_t res_index,
                           mrt_task_id_t task, int32_t *value, int *received)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || value == NULL || received == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_MSGQ, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    if (mrt_task_ref(kernel, task) == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *value = 0;   /* deterministic output even on the blocking path */
    *received = 0;

    if (r->msg_count > 0) {
        *value = r->msg_buf[r->msg_head];
        r->msg_head = (r->msg_head + 1) % r->msg_capacity;
        r->msg_count--;
        r->acquisitions++;
        *received = 1;

        if (r->waiter_count > 0u) {
            /* a blocked sender can retry now: wake FIFO-first. There is no
             * output parameter for this wake — the engine observes the
             * sender's READY state and re-runs its send step on dispatch. */
            mrt_task_id_t id = r->waiters[0];
            rc = mrt_task_set_state(kernel, id, MRT_TASK_READY, k->now);
            if (rc != MRT_OK) {
                return rc; /* defensive */
            }
            wremove_at(r, 0u);
        }
        return MRT_OK;
    }

    /* empty: receiver blocks (FIFO order) */
    if (wfind(r, task) >= 0) {
        return MRT_ERR_DUPLICATE; /* defensive */
    }
    if (r->waiter_count >= MRT_MAX_WAITERS) {
        return MRT_ERR_FULL;
    }
    rc = mrt_task_set_state(kernel, task, MRT_TASK_BLOCKED, k->now);
    if (rc != MRT_OK) {
        return rc;
    }
    wappend(r, task);
    r->contentions++;
    return MRT_OK;
}

mrt_result_t mrt_msgq_peek(const void *kernel, uint32_t res_index, int32_t *value)
{
    const mrt_kernel_t *k = (const mrt_kernel_t *)kernel;
    if (k == NULL || value == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (res_index >= k->resource_count) {
        return MRT_ERR_NOT_FOUND;
    }
    const mrt_resource_t *r = &k->resources[res_index];
    if (r->kind != MRT_RES_MSGQ) {
        return MRT_ERR_INVALID_ARG;
    }
    if (r->msg_count <= 0) {
        return MRT_ERR_NOT_FOUND;
    }
    *value = r->msg_buf[r->msg_head];
    return MRT_OK;
}

/* ------------------------------------------------------------------ */
/* event flags                                                         */
/* ------------------------------------------------------------------ */

/* engine_ctx packing — see the convention note at the top of this file. */
#define MRT_EVCTX_MASK(u)   ((uint32_t)((u) & UINT64_C(0xFFFFFFFF)))
#define MRT_EVCTX_ALL(u)    (uint32_t)(((u) >> 32) & UINT64_C(1))
#define MRT_EVCTX_PACK(m, a) \
    ((uint64_t)(uint32_t)(m) | ((uint64_t)((a) ? 1u : 0u) << 32))

static int evcond(uint32_t flags, uint32_t mask, int all_bits)
{
    if (all_bits) {
        return (flags & mask) == mask;
    }
    return (flags & mask) != 0u;
}

mrt_result_t mrt_evflags_wait(void *kernel, uint32_t res_index,
                              mrt_task_id_t task, uint32_t mask,
                              int all_bits, int *satisfied)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || satisfied == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_EVENTFLAGS, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    mrt_task_t *t = mrt_task_ref(kernel, task);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }
    *satisfied = 0;

    if (evcond(r->evflags, mask, all_bits)) {
        r->acquisitions++;
        *satisfied = 1; /* state unchanged */
        return MRT_OK;
    }

    /* unsatisfied: block and record (mask, mode) in engine_ctx */
    if (wfind(r, task) >= 0) {
        return MRT_ERR_DUPLICATE; /* defensive */
    }
    if (r->waiter_count >= MRT_MAX_WAITERS) {
        return MRT_ERR_FULL;
    }
    rc = mrt_task_set_state(kernel, task, MRT_TASK_BLOCKED, k->now);
    if (rc != MRT_OK) {
        return rc;
    }
    t->engine_ctx = MRT_EVCTX_PACK(mask, all_bits);
    wappend(r, task); /* FIFO arrival order */
    r->contentions++;
    return MRT_OK;
}

mrt_result_t mrt_evflags_set(void *kernel, uint32_t res_index, uint32_t mask,
                             mrt_task_id_t *woken, uint32_t woken_cap,
                             uint32_t *woken_count)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || woken_count == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (woken_cap > 0u && woken == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_EVENTFLAGS, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    *woken_count = 0u;

    r->evflags |= mask;

    /* wake every waiter whose condition now holds (list order) */
    uint32_t i = 0u;
    while (i < r->waiter_count) {
        mrt_task_t *wt = mrt_task_ref(kernel, r->waiters[i]);
        if (wt == NULL) {
            i++;
            continue;
        }
        if (evcond(r->evflags, MRT_EVCTX_MASK(wt->engine_ctx),
                   (int)MRT_EVCTX_ALL(wt->engine_ctx))) {
            mrt_task_id_t id = r->waiters[i];
            rc = mrt_task_set_state(kernel, id, MRT_TASK_READY, k->now);
            if (rc != MRT_OK) {
                i++; /* defensive: skip instead of failing the whole set */
                continue;
            }
            wremove_at(r, i); /* next waiter shifted into slot i — no i++ */
            r->acquisitions++; /* woken task's wait is satisfied */
            if (*woken_count < woken_cap) {
                woken[*woken_count] = id;
            }
            (*woken_count)++;
        } else {
            i++;
        }
    }
    return MRT_OK;
}

mrt_result_t mrt_evflags_clear(void *kernel, uint32_t res_index, uint32_t mask)
{
    mrt_resource_t *r = NULL;
    mrt_result_t rc = res_check(kernel, res_index, MRT_RES_EVENTFLAGS, &r);
    if (rc != MRT_OK) {
        return rc;
    }
    r->evflags &= ~mask; /* clearing can never satisfy a waiter */
    return MRT_OK;
}
