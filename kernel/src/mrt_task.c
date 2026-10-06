/* mrt_task.c — task table, validated state machine, priority handling.
 *
 * Implementation notes (2-a):
 *  - The task table is append-only: there is no unregister API in the
 *    frozen contract, so slot == task_count at registration time.
 *  - Duplicate names are detected on TRUNCATED names (both sides), so two
 *    engine ids sharing a 31-char prefix cannot both register. mrt_task_find
 *    matches the same way, keeping register/find consistent.
 *  - The state machine performs accounting for the OLD state before
 *    switching. Time is expected monotone; a non-monotone `now` is clamped
 *    to a zero delta (defensive, deterministic — never wraps).
 *  - READY-queue MEMBERSHIP is deliberately not managed here: the engine
 *    calls mrt_readyq_push when a task becomes READY and mrt_readyq_pop on
 *    dispatch. mrt_task_set_state only tracks state + statistics.
 *  - effective priority formula (recompute):
 *        eff = clamp(max(base + aging_units, max effective priority of
 *                        tasks waiting on INHERIT mutexes owned by the
 *                        task), MRT_PRIO_MIN, MRT_PRIO_MAX)
 *    Aging applies to the base term only; inherited boosts are absolute.
 *    (The header one-liner reads max(base, inherited) + aging; the task-2a
 *    specification refines it to the formula above, which never inflates
 *    an inherited priority by aging. Documented in worklog.)
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "mrt_internal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void name_copy(char dst[MRT_MAX_NAME], const char *src)
{
    uint32_t i = 0u;
    while ((i + 1u < MRT_MAX_NAME) && (src[i] != '\0')) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static mrt_prio_t prio_clamp(int64_t v)
{
    if (v < (int64_t)MRT_PRIO_MIN) {
        return (mrt_prio_t)MRT_PRIO_MIN;
    }
    if (v > (int64_t)MRT_PRIO_MAX) {
        return (mrt_prio_t)MRT_PRIO_MAX;
    }
    return (mrt_prio_t)v;
}

mrt_task_t *mrt_task_ref(const void *kernel, mrt_task_id_t id)
{
    /* const marks read-only intent at the call site; the kernel object is
     * mutable at origin, so this cast is safe for the public const APIs. */
    mrt_kernel_t *k = (mrt_kernel_t *)(uintptr_t)kernel;
    if (k == NULL) {
        return NULL;
    }
    if ((uint32_t)id >= k->task_count || (uint32_t)id >= MRT_MAX_TASKS) {
        return NULL;
    }
    mrt_task_t *t = &k->tasks[id];
    return (t->id == id) ? t : NULL;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

mrt_result_t mrt_task_register(void *kernel, const char *name,
                               mrt_task_kind_t kind, mrt_prio_t base_priority,
                               mrt_time_t arrival, mrt_time_t period,
                               mrt_time_t relative_deadline,
                               mrt_task_id_t *out_id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || name == NULL || out_id == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (kind != MRT_TASK_APERIODIC && kind != MRT_TASK_PERIODIC &&
        kind != MRT_TASK_SPORADIC) {
        return MRT_ERR_INVALID_ARG;
    }
    if (k->task_count >= MRT_MAX_TASKS) {
        return MRT_ERR_FULL;
    }

    /* duplicate check on truncated names (see file header) */
    {
        char q[MRT_MAX_NAME];
        name_copy(q, name);
        for (uint32_t i = 0u; i < k->task_count; i++) {
            if ((k->tasks[i].id == (mrt_task_id_t)i) &&
                (strcmp(k->tasks[i].name, q) == 0)) {
                return MRT_ERR_DUPLICATE;
            }
        }
    }

    uint32_t slot = k->task_count; /* append-only table */
    mrt_task_t *t = &k->tasks[slot];
    memset(t, 0, sizeof *t);
    t->id = (mrt_task_id_t)slot;
    name_copy(t->name, name);
    t->kind = kind;
    t->base_priority = prio_clamp((int64_t)base_priority);
    t->effective_priority = t->base_priority;
    t->state = MRT_TASK_UNUSED;
    t->arrival = arrival;
    t->period = period;
    t->relative_deadline = relative_deadline;
    k->task_count++;
    *out_id = t->id;
    return MRT_OK;
}

mrt_task_t *mrt_task_get(void *kernel, mrt_task_id_t id)
{
    return mrt_task_ref(kernel, id);
}

mrt_task_id_t mrt_task_find(void *kernel, const char *name)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL || name == NULL) {
        return MRT_TASK_ID_NONE;
    }
    {
        char q[MRT_MAX_NAME];
        name_copy(q, name); /* find matches on truncated names too */
        for (uint32_t i = 0u; i < k->task_count; i++) {
            if ((k->tasks[i].id == (mrt_task_id_t)i) &&
                (strcmp(k->tasks[i].name, q) == 0)) {
                return k->tasks[i].id;
            }
        }
    }
    return MRT_TASK_ID_NONE;
}

/* ------------------------------------------------------------------ */
/* state machine                                                       */
/* ------------------------------------------------------------------ */

mrt_result_t mrt_task_set_state(void *kernel, mrt_task_id_t id,
                                mrt_task_state_t new_state, mrt_time_t now)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if ((int)new_state < 0 || new_state >= MRT_TASK_STATE_COUNT) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_task_t *t = mrt_task_ref(kernel, id);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }

    mrt_task_state_t old = t->state;

    /* RUNNING->RUNNING is the single legal no-op: MRT_OK, no accounting. */
    if (old == MRT_TASK_RUNNING && new_state == MRT_TASK_RUNNING) {
        return MRT_OK;
    }

    int legal = 0;
    switch (old) {
    case MRT_TASK_UNUSED:
        legal = (new_state == MRT_TASK_READY); /* job release */
        break;
    case MRT_TASK_READY:
        legal = (new_state == MRT_TASK_RUNNING); /* dispatch */
        break;
    case MRT_TASK_RUNNING:
        legal = (new_state == MRT_TASK_READY || /* preempted / quantum   */
                 new_state == MRT_TASK_BLOCKED || /* resource wait        */
                 new_state == MRT_TASK_WAITING || /* I/O burst            */
                 new_state == MRT_TASK_SLEEPING || /* timed sleep          */
                 new_state == MRT_TASK_TERMINATED); /* job completion      */
        break;
    case MRT_TASK_BLOCKED:
    case MRT_TASK_WAITING:
    case MRT_TASK_SLEEPING:
        legal = (new_state == MRT_TASK_READY);
        break;
    case MRT_TASK_TERMINATED:
    default:
        legal = 0; /* TERMINATED is terminal (see header recycling note) */
        break;
    }
    if (!legal) {
        return MRT_ERR_BAD_STATE;
    }

    /* (a) accumulate residence time of the OLD state */
    mrt_time_t delta =
        (now >= t->state_entered_at) ? (now - t->state_entered_at) : 0u;
    switch (old) {
    case MRT_TASK_READY:
        t->ready_wait_total += delta;
        break;
    case MRT_TASK_RUNNING:
        t->cpu_time_total += delta;
        break;
    case MRT_TASK_BLOCKED:
        t->blocked_total += delta;
        break;
    case MRT_TASK_WAITING:
        t->waiting_total += delta;
        break;
    case MRT_TASK_SLEEPING:
        t->sleeping_total += delta;
        break;
    default:
        break; /* UNUSED / TERMINATED carry no time counter */
    }

    /* (b) switch, (c) restamp */
    t->state = new_state;
    t->state_entered_at = now;

    if (new_state == MRT_TASK_READY) {
        t->last_ready_at = now;
        if (old == MRT_TASK_UNUSED) {
            t->jobs_released++; /* exactly one per job release */
        }
    }
    if (new_state == MRT_TASK_RUNNING && t->dispatched_once == 0u) {
        t->first_dispatch = now;
        t->dispatched_once = 1u;
    }
    return MRT_OK;
}

/* ------------------------------------------------------------------ */
/* priority handling                                                   */
/* ------------------------------------------------------------------ */

void mrt_task_priority_changed(void *kernel, mrt_task_id_t id)
{
    /* Re-sort the ready queue if queued (NOT_FOUND when not queued — that
     * is fine and expected). Also keep priority-ordered waiter lists
     * consistent so waiters[0] is always the legitimate next owner. */
    (void)mrt_readyq_reposition(kernel, id);
    mrt_sync_resort_waiter_lists(kernel, id);
}

mrt_result_t mrt_task_recompute_priority(void *kernel, mrt_task_id_t id)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_task_t *t = mrt_task_ref(kernel, id);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }

    mrt_prio_t inherited = t->base_priority;
    for (uint32_t r = 0u; r < k->resource_count; r++) {
        const mrt_resource_t *res = &k->resources[r];
        if (res->kind != MRT_RES_MUTEX || res->protocol != MRT_PROTO_INHERIT) {
            continue;
        }
        if (res->owner != id) {
            continue;
        }
        for (uint32_t w = 0u; w < res->waiter_count; w++) {
            mrt_task_t *wt = mrt_task_ref(kernel, res->waiters[w]);
            if (wt != NULL && wt->effective_priority > inherited) {
                inherited = wt->effective_priority;
            }
        }
    }

    int64_t base_with_aging = (int64_t)t->base_priority + (int64_t)t->aging_units;
    int64_t v = (inherited > base_with_aging) ? (int64_t)inherited : base_with_aging;
    t->effective_priority = prio_clamp(v);

    mrt_task_priority_changed(kernel, id);
    return MRT_OK;
}

mrt_result_t mrt_task_boost(void *kernel, mrt_task_id_t id, mrt_prio_t add)
{
    mrt_kernel_t *k = (mrt_kernel_t *)kernel;
    if (k == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    mrt_task_t *t = mrt_task_ref(kernel, id);
    if (t == NULL) {
        return MRT_ERR_NOT_FOUND;
    }

    /* Aging is an additive term on the BASE priority. It can raise
     * effective priority above base but never pull it below base:
     * rollback saturates aging_units at 0 and recompute restores the
     * base floor (inherited boosts from mutexes are handled by the
     * recompute itself). */
    if (add > 0) {
        t->aging_units += (uint32_t)add;
    } else if (add < 0) {
        uint32_t drop = (uint32_t)(-(int64_t)add);
        t->aging_units = (t->aging_units > drop) ? (t->aging_units - drop) : 0u;
    }
    return mrt_task_recompute_priority(kernel, id);
}
