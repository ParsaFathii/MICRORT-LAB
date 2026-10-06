/* mrt_mem.h — MicroRT-Lab C kernel: memory allocators.
 *
 * Two deterministic allocator models, both simulation-safe (no real
 * pointer arithmetic is exposed; offsets/handles only):
 *
 *  1. mrt_pool_t — fixed-size block pool: O(1) alloc/free, exhibits
 *     INTERNAL fragmentation (request smaller than block wastes space).
 *  2. mrt_mem_t  — variable-size region with coalescing free lists and
 *     selectable FIRST / BEST / WORST fit policy; exhibits EXTERNAL
 *     fragmentation (free space split into unusably small pieces).
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_MRT_MEM_H
#define MICRORT_MRT_MEM_H

#include "mrt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Fixed-size block pool                                               */
/* ------------------------------------------------------------------ */
#define MRT_POOL_BITMAP_LIMBS 8u   /* 8 * 64 = 512 blocks max          */

typedef struct mrt_pool {
    size_t   block_size;
    size_t   block_count;
    uint64_t used_map[MRT_POOL_BITMAP_LIMBS];
} mrt_pool_t;

/* Returns MRT_OK and index in *out_index, or MRT_ERR_NO_MEMORY. */
mrt_result_t mrt_pool_alloc(mrt_pool_t *p, size_t *out_index);
mrt_result_t mrt_pool_free(mrt_pool_t *p, size_t index);
void mrt_pool_init(mrt_pool_t *p, size_t block_size, size_t block_count);

size_t mrt_pool_used(const mrt_pool_t *p);
size_t mrt_pool_free_count(const mrt_pool_t *p);
/* Sum of block indices currently allocated (diagnostics). */
size_t mrt_pool_used_bytes(const mrt_pool_t *p);

/* ------------------------------------------------------------------ */
/* Variable-size region (first/best/worst fit, coalescing)             */
/* ------------------------------------------------------------------ */
typedef enum mrt_fit_policy {
    MRT_FIT_FIRST = 0,
    MRT_FIT_BEST = 1,
    MRT_FIT_WORST = 2
} mrt_fit_policy_t;

#define MRT_MEM_MAX_BLOCKS 256u

typedef struct mrt_mem_block {
    size_t  offset;
    size_t  size;
    uint8_t used;              /* 0 = free, 1 = allocated              */
    uint8_t pad[7];
} mrt_mem_block_t;

typedef struct mrt_mem {
    size_t            total;
    mrt_fit_policy_t  policy;
    mrt_mem_block_t   blocks[MRT_MEM_MAX_BLOCKS];
    uint32_t          block_count;   /* live entries (used + free)     */
    uint32_t          alloc_count;   /* successful allocations         */
    uint32_t          fail_count;    /* failed allocations             */
} mrt_mem_t;

/* Creates one free block spanning [0, total). */
void mrt_mem_init(mrt_mem_t *m, size_t total, mrt_fit_policy_t policy);

/* Allocates `size` (size>0). *out_offset receives the block start.
 * MRT_ERR_NO_MEMORY when no fit (or block table exhausted). */
mrt_result_t mrt_mem_alloc(mrt_mem_t *m, size_t size, size_t *out_offset);

/* Frees the allocated block starting exactly at `offset` and coalesces
 * with free neighbours. MRT_ERR_NOT_FOUND when no allocated block
 * starts there. */
mrt_result_t mrt_mem_free(mrt_mem_t *m, size_t offset);

/* The allocated block starting at `offset` (for engine bookkeeping). */
mrt_result_t mrt_mem_block_at(const mrt_mem_t *m, size_t offset,
                              size_t *out_size);

size_t mrt_mem_used(const mrt_mem_t *m);
size_t mrt_mem_free_total(const mrt_mem_t *m);
size_t mrt_mem_largest_free(const mrt_mem_t *m);
/* External fragmentation: free_total - largest_free (0 when free space
 * is one contiguous block). */
size_t mrt_mem_fragmentation(const mrt_mem_t *m);
/* Live block entries for visualization. */
uint32_t mrt_mem_blocks(const mrt_mem_t *m, const mrt_mem_block_t **out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MICRORT_MRT_MEM_H */
