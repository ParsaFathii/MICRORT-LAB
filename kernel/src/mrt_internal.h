/* mrt_internal.h — MicroRT-Lab kernel: private shared helpers.
 *
 * NOT part of the public API: these symbols exist only for the kernel's
 * own translation units (kernel/src dot-c files). The file lives under kernel/src
 * (not kernel/include) on purpose — engine and tests must not see it.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_INTERNAL_H
#define MICRORT_MRT_INTERNAL_H

#include "micrort/mrt_kernel.h"

/* Validated task lookup: returns NULL when kernel or task id is unknown.
 * The const qualifier marks read-only intent at call sites; the kernel
 * object itself is mutable (public const APIs rely on this helper). */
mrt_task_t *mrt_task_ref(const void *kernel, mrt_task_id_t id);

/* Notify the kernel that a task's effective priority changed: repositions
 * it in the ready queue (if queued) and re-sorts any priority-ordered
 * waiter list containing it. Called by recompute/boost/inheritance. */
void mrt_task_priority_changed(void *kernel, mrt_task_id_t id);

/* Re-sort the (priority-ordered) waiter lists containing `id`; FIFO-ordered
 * lists (msgq / evflags) are left alone because priority is not their key. */
void mrt_sync_resort_waiter_lists(void *kernel, mrt_task_id_t id);

#endif /* MICRORT_MRT_INTERNAL_H */
