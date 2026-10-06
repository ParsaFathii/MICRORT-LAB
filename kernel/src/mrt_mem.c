/* mrt_mem.c — fixed-size block pool (bitmap) and variable-size region
 * allocator (first/best/worst fit with coalescing).
 *
 * Pool: O(1)-ish alloc/free via a 512-bit bitmap (8 limbs x 64 bits);
 * block_count is clamped to 512 at init. alloc always returns the LOWEST
 * free index (deterministic). used_bytes = used blocks * block_size (see
 * the header doc fix).
 *
 * Region: a sorted-by-offset block table. Free coalesces with adjacent
 * free neighbours (offset+size adjacency, both sides). When a split would
 * be needed (remainder > 0) but the block table is exhausted, alloc fails
 * with MRT_ERR_NO_MEMORY per mrt_mem.h ("no fit (or block table
 * exhausted)"); exact fits still succeed because they need no new entry.
 * [2-a note: the instruction sketch suggested "take the whole block" when
 * the table is full; the frozen header doc is authoritative instead.]
 * fail_count++ exactly on every MRT_ERR_NO_MEMORY return.
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#include "micrort/mrt_mem.h"

#include <string.h>

#define POOL_MAX_BLOCKS ((size_t)MRT_POOL_BITMAP_LIMBS * 64u)

/* ------------------------------------------------------------------ */
/* fixed-size block pool                                               */
/* ------------------------------------------------------------------ */

void mrt_pool_init(mrt_pool_t *p, size_t block_size, size_t block_count)
{
    if (p == NULL) {
        return;
    }
    p->block_size = block_size;
    p->block_count = (block_count > POOL_MAX_BLOCKS) ? POOL_MAX_BLOCKS
                                                     : block_count;
    for (uint32_t i = 0u; i < MRT_POOL_BITMAP_LIMBS; i++) {
        p->used_map[i] = UINT64_C(0);
    }
}

mrt_result_t mrt_pool_alloc(mrt_pool_t *p, size_t *out_index)
{
    if (p == NULL || out_index == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    for (uint32_t limb = 0u; limb < MRT_POOL_BITMAP_LIMBS; limb++) {
        for (uint32_t b = 0u; b < 64u; b++) {
            size_t idx = (size_t)limb * 64u + (size_t)b;
            if (idx >= p->block_count) {
                break; /* beyond pool capacity */
            }
            if (((p->used_map[limb] >> b) & UINT64_C(1)) == UINT64_C(0)) {
                p->used_map[limb] |= (UINT64_C(1) << b);
                *out_index = idx;
                return MRT_OK;
            }
        }
    }
    return MRT_ERR_NO_MEMORY;
}

mrt_result_t mrt_pool_free(mrt_pool_t *p, size_t index)
{
    if (p == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (index >= p->block_count) {
        return MRT_ERR_INVALID_ARG; /* bad handle value */
    }
    uint32_t limb = (uint32_t)(index / 64u);
    uint32_t b = (uint32_t)(index % 64u);
    if (((p->used_map[limb] >> b) & UINT64_C(1)) == UINT64_C(0)) {
        return MRT_ERR_NOT_FOUND; /* double free */
    }
    p->used_map[limb] &= ~(UINT64_C(1) << b);
    return MRT_OK;
}

size_t mrt_pool_used(const mrt_pool_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    size_t n = 0u;
    for (uint32_t limb = 0u; limb < MRT_POOL_BITMAP_LIMBS; limb++) {
        uint64_t v = p->used_map[limb]; /* bits >= block_count never set */
        while (v != UINT64_C(0)) {
            v &= (v - UINT64_C(1)); /* clears lowest set bit */
            n++;
        }
    }
    return n;
}

size_t mrt_pool_free_count(const mrt_pool_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    return p->block_count - mrt_pool_used(p);
}

size_t mrt_pool_used_bytes(const mrt_pool_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    return mrt_pool_used(p) * p->block_size;
}

/* ------------------------------------------------------------------ */
/* variable-size region                                                */
/* ------------------------------------------------------------------ */

void mrt_mem_init(mrt_mem_t *m, size_t total, mrt_fit_policy_t policy)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof *m);
    m->total = total;
    m->policy = policy;
    if (total > 0u) {
        m->blocks[0].offset = 0u;
        m->blocks[0].size = total;
        m->blocks[0].used = 0u;
        m->block_count = 1u;
    }
}

static void mem_shift_up(mrt_mem_t *m, uint32_t from)
{
    for (uint32_t j = m->block_count; j > from; j--) {
        m->blocks[j] = m->blocks[j - 1u];
    }
}

static void mem_shift_down(mrt_mem_t *m, uint32_t from)
{
    for (uint32_t j = from; (j + 1u) < m->block_count; j++) {
        m->blocks[j] = m->blocks[j + 1u];
    }
}

mrt_result_t mrt_mem_alloc(mrt_mem_t *m, size_t size, size_t *out_offset)
{
    if (m == NULL || out_offset == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    if (size == 0u) {
        return MRT_ERR_INVALID_ARG;
    }

    /* blocks are sorted by offset: FIRST fit = first fitting entry. */
    int64_t best = -1;
    for (uint32_t i = 0u; i < m->block_count; i++) {
        const mrt_mem_block_t *b = &m->blocks[i];
        if (b->used != 0u || b->size < size) {
            continue;
        }
        if (m->policy == MRT_FIT_FIRST) {
            best = (int64_t)i;
            break;
        }
        if (best < 0) {
            best = (int64_t)i;
            continue;
        }
        const mrt_mem_block_t *cur = &m->blocks[best];
        if (m->policy == MRT_FIT_BEST && b->size < cur->size) {
            best = (int64_t)i; /* smallest fitting; ties -> lowest offset */
        } else if (m->policy == MRT_FIT_WORST && b->size > cur->size) {
            best = (int64_t)i; /* largest fitting; ties -> lowest offset */
        }
    }
    if (best < 0) {
        m->fail_count++;
        return MRT_ERR_NO_MEMORY;
    }

    uint32_t i = (uint32_t)best;
    size_t remainder = m->blocks[i].size - size;
    if (remainder > 0u) {
        if (m->block_count >= MRT_MEM_MAX_BLOCKS) {
            /* table exhausted and a split would need a new entry (see the
             * file header note); exact fits never reach this point */
            m->fail_count++;
            return MRT_ERR_NO_MEMORY;
        }
        mem_shift_up(m, i + 1u);
        m->blocks[i + 1u].offset = m->blocks[i].offset + size;
        m->blocks[i + 1u].size = remainder;
        m->blocks[i + 1u].used = 0u;
        m->block_count++;
        m->blocks[i].size = size;
    }
    m->blocks[i].used = 1u;
    m->alloc_count++;
    *out_offset = m->blocks[i].offset;
    return MRT_OK;
}

mrt_result_t mrt_mem_free(mrt_mem_t *m, size_t offset)
{
    if (m == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    int64_t idx = -1;
    for (uint32_t i = 0u; i < m->block_count; i++) {
        if (m->blocks[i].offset == offset) {
            if (m->blocks[i].used != 0u) {
                idx = (int64_t)i;
            }
            break; /* offsets are unique (table sorted) */
        }
    }
    if (idx < 0) {
        return MRT_ERR_NOT_FOUND; /* no ALLOCATED block starts there */
    }
    uint32_t i = (uint32_t)idx;
    m->blocks[i].used = 0u;

    /* merge right neighbour (adjacent free block) */
    if ((i + 1u) < m->block_count) {
        mrt_mem_block_t *b = &m->blocks[i];
        mrt_mem_block_t *n = &m->blocks[i + 1u];
        if (n->used == 0u && n->offset == b->offset + b->size) {
            b->size += n->size;
            mem_shift_down(m, i + 1u);
            m->block_count--;
        }
    }
    /* merge left neighbour */
    if (i > 0u) {
        mrt_mem_block_t *b = &m->blocks[i];
        mrt_mem_block_t *l = &m->blocks[i - 1u];
        if (l->used == 0u && b->offset == l->offset + l->size) {
            l->size += b->size;
            mem_shift_down(m, i);
            m->block_count--;
        }
    }
    return MRT_OK;
}

mrt_result_t mrt_mem_block_at(const mrt_mem_t *m, size_t offset, size_t *out_size)
{
    if (m == NULL || out_size == NULL) {
        return MRT_ERR_INVALID_ARG;
    }
    for (uint32_t i = 0u; i < m->block_count; i++) {
        if (m->blocks[i].offset == offset && m->blocks[i].used != 0u) {
            *out_size = m->blocks[i].size;
            return MRT_OK;
        }
    }
    return MRT_ERR_NOT_FOUND;
}

size_t mrt_mem_used(const mrt_mem_t *m)
{
    if (m == NULL) {
        return 0u;
    }
    size_t s = 0u;
    for (uint32_t i = 0u; i < m->block_count; i++) {
        if (m->blocks[i].used != 0u) {
            s += m->blocks[i].size;
        }
    }
    return s;
}

size_t mrt_mem_free_total(const mrt_mem_t *m)
{
    if (m == NULL) {
        return 0u;
    }
    size_t s = 0u;
    for (uint32_t i = 0u; i < m->block_count; i++) {
        if (m->blocks[i].used == 0u) {
            s += m->blocks[i].size;
        }
    }
    return s;
}

size_t mrt_mem_largest_free(const mrt_mem_t *m)
{
    if (m == NULL) {
        return 0u;
    }
    size_t s = 0u;
    for (uint32_t i = 0u; i < m->block_count; i++) {
        if (m->blocks[i].used == 0u && m->blocks[i].size > s) {
            s = m->blocks[i].size;
        }
    }
    return s;
}

size_t mrt_mem_fragmentation(const mrt_mem_t *m)
{
    if (m == NULL) {
        return 0u;
    }
    return mrt_mem_free_total(m) - mrt_mem_largest_free(m);
}

uint32_t mrt_mem_blocks(const mrt_mem_t *m, const mrt_mem_block_t **out)
{
    if (m == NULL || out == NULL) {
        return 0u;
    }
    *out = m->blocks;
    return m->block_count;
}
