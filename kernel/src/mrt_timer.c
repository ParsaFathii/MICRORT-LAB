/* mrt_timer.c — sorted one-shot timer list.
 *
 * The list is a compact array kept sorted by fire_at ASC; insertion is
 * stable (an existing timer with an equal fire_at stays before the newly
 * inserted one — ties fire in insertion order).
 *
 * HANDLE SEMANTICS (important, 2-a): a handle is (array index + 1) at the
 * time of the add. Every removal (cancel, pop_expired, cancel_all_for)
 * COMPACTS the array and therefore INVALIDATES every outstanding handle:
 * handles are only valid until the next mutation. The engine must not
 * cache handles across mutations; it re-scans via mrt_timer_next /
 * mrt_timer_pop_expired / l->count. This is the documented simple scheme
 * chosen for the frozen API (no per-timer stable ids exist).
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "micrort/mrt_timer.h"

void mrt_timer_list_init(mrt_timer_list_t *l)
{
    if (l == NULL) {
        return;
    }
    for (uint32_t i = 0u; i < MRT_MAX_TIMERS; i++) {
        l->items[i].fire_at = 0u;
        l->items[i].task = MRT_TASK_ID_NONE;
        l->items[i].tag = 0u;
        l->items[i].active = 0u;
    }
    l->count = 0u;
}

mrt_result_t mrt_timer_add(mrt_timer_list_t *l, mrt_time_t fire_at,
                           mrt_task_id_t task, mrt_timer_tag_t tag,
                           uint32_t *out_handle)
{
    if (l == NULL || out_handle == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (l->count >= MRT_MAX_TIMERS) {
        return MRT_ERR_FULL;
    }
    uint32_t pos = l->count;
    for (uint32_t i = 0u; i < l->count; i++) {
        if (l->items[i].fire_at > fire_at) {
            pos = i; /* strictly greater keeps equal-time entries first */
            break;
        }
    }
    for (uint32_t j = l->count; j > pos; j--) {
        l->items[j] = l->items[j - 1u];
    }
    l->items[pos].fire_at = fire_at;
    l->items[pos].task = task;
    l->items[pos].tag = tag;
    l->items[pos].active = 1u;
    l->count++;
    *out_handle = pos + 1u; /* index+1 (see handle semantics above) */
    return MRT_OK;
}

mrt_result_t mrt_timer_cancel(mrt_timer_list_t *l, uint32_t handle)
{
    if (l == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (handle == 0u || handle > l->count) {
        return MRT_ERR_NOT_FOUND;
    }
    uint32_t i = handle - 1u;
    if (l->items[i].active == 0u) {
        return MRT_ERR_NOT_FOUND;
    }
    for (uint32_t j = i; (j + 1u) < l->count; j++) {
        l->items[j] = l->items[j + 1u];
    }
    l->count--;
    l->items[l->count].active = 0u; /* hygiene on the vacated slot */
    return MRT_OK;
}

mrt_time_t mrt_timer_next(const mrt_timer_list_t *l)
{
    if (l == NULL || l->count == 0u) {
        return MRT_TIME_MAX;
    }
    return l->items[0].fire_at;
}

mrt_result_t mrt_timer_pop_expired(mrt_timer_list_t *l, mrt_time_t now,
                                   mrt_timer_t *out_timer)
{
    if (l == NULL || out_timer == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (l->count == 0u || l->items[0].fire_at > now) {
        return MRT_ERR_NOT_FOUND; /* nothing due yet */
    }
    *out_timer = l->items[0];
    for (uint32_t j = 0u; (j + 1u) < l->count; j++) {
        l->items[j] = l->items[j + 1u];
    }
    l->count--;
    l->items[l->count].active = 0u;
    return MRT_OK;
}

void mrt_timer_cancel_all_for(mrt_timer_list_t *l, mrt_task_id_t task)
{
    if (l == NULL) {
        return;
    }
    uint32_t i = 0u;
    while (i < l->count) {
        if (l->items[i].task == task) {
            for (uint32_t j = i; (j + 1u) < l->count; j++) {
                l->items[j] = l->items[j + 1u];
            }
            l->count--;
        } else {
            i++;
        }
    }
    if (l->count < MRT_MAX_TIMERS) {
        l->items[l->count].active = 0u; /* hygiene */
    }
}
