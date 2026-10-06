/* test_mem.c — block pool + region allocator: fit policies, coalescing,
 * fragmentation, exhaustion, error paths.

Copyright © 2026 Parsa Fathi. Apache-2.0. */
#define TH_SUITE "test_mem"
#include "harness.h"
#include "micrort/mrt_mem.h"

int main(void)
{
    /* ============ pool basics ============ */
    TEST_NAME("pool-alloc-free");
    mrt_pool_t p;
    mrt_pool_init(&p, 64, 4);
    TEST_ASSERT_EQ_SIZE(0, mrt_pool_used(&p));
    TEST_ASSERT_EQ_SIZE(4, mrt_pool_free_count(&p));
    TEST_ASSERT_EQ_SIZE(0, mrt_pool_used_bytes(&p));

    size_t i0 = 999, i1 = 999, i2 = 999, i3 = 999, i4 = 999;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_alloc(&p, &i0));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_alloc(&p, &i1));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_alloc(&p, &i2));
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_alloc(&p, &i3));
    TEST_ASSERT_EQ_INT(MRT_ERR_NO_MEMORY, mrt_pool_alloc(&p, &i4));
    TEST_ASSERT_EQ_SIZE(0, i0); /* lowest free index first (deterministic) */
    TEST_ASSERT_EQ_SIZE(1, i1);
    TEST_ASSERT_EQ_SIZE(2, i2);
    TEST_ASSERT_EQ_SIZE(3, i3);
    TEST_ASSERT_EQ_SIZE(4, mrt_pool_used(&p));
    TEST_ASSERT_EQ_SIZE(0, mrt_pool_free_count(&p));
    TEST_ASSERT_EQ_SIZE(256, mrt_pool_used_bytes(&p)); /* 4 * 64 */

    /* free + realloc same index */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_free(&p, 1));
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_pool_free(&p, 1)); /* double free */
    size_t i5 = 999;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_pool_alloc(&p, &i5));
    TEST_ASSERT_EQ_SIZE(1, i5);

    /* error paths */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_pool_free(&p, 4));   /* oob */
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_pool_alloc(NULL, &i5));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_pool_alloc(&p, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_pool_free(NULL, 0));
    mrt_pool_init(NULL, 1, 1); /* must not crash */
    TEST_ASSERT_EQ_SIZE(0, mrt_pool_used(NULL));

    /* count clamped to bitmap capacity (8 limbs * 64 = 512) */
    mrt_pool_t big;
    mrt_pool_init(&big, 8, 600);
    TEST_ASSERT_EQ_INT(0, (int)(big.block_count > 512)); /* clamped silently */

    /* ============ region: first fit ============ */
    TEST_NAME("region-first-fit");
    mrt_mem_t m;
    mrt_mem_init(&m, 100, MRT_FIT_FIRST);
    TEST_ASSERT_EQ_SIZE(100, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(0, mrt_mem_used(&m));
    TEST_ASSERT_EQ_SIZE(0, mrt_mem_fragmentation(&m));
    TEST_ASSERT_EQ_SIZE(100, mrt_mem_largest_free(&m));

    size_t o1 = 999, o2 = 999, o3 = 999;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 30, &o1));
    TEST_ASSERT_EQ_SIZE(0, o1);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 20, &o2));
    TEST_ASSERT_EQ_SIZE(30, o2);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 50, &o3));
    TEST_ASSERT_EQ_SIZE(50, o3);
    TEST_ASSERT_EQ_SIZE(100, mrt_mem_used(&m));
    TEST_ASSERT_EQ_INT(0, (int)m.alloc_count - 3);

    /* free middle, then alloc smaller (first fit takes the hole) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_free(&m, 30));
    size_t o4 = 999;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 10, &o4));
    TEST_ASSERT_EQ_SIZE(30, o4); /* first fit -> lowest-offset hole */
    TEST_ASSERT_EQ_INT(0, (int)m.fail_count);
    /* 10 bytes remain in the hole: an 11-byte request must fail */
    TEST_ASSERT_EQ_INT(MRT_ERR_NO_MEMORY, mrt_mem_alloc(&m, 11, &o4));
    TEST_ASSERT_EQ_INT(1, (int)m.fail_count);

    /* free unknown offset / bad args */
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mem_free(&m, 77));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mem_alloc(&m, 0, &o4));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mem_alloc(NULL, 4, &o4));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mem_alloc(&m, 4, NULL));
    TEST_ASSERT_EQ_INT(MRT_ERR_INVALID_ARG, mrt_mem_free(NULL, 0));
    /* no allocated block starts at (size_t)-1: NOT_FOUND, not INVALID */
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mem_free(&m, (size_t)-1));
    mrt_mem_init(NULL, 10, MRT_FIT_FIRST); /* must not crash */

    /* block_at */
    size_t sz = 0;
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_block_at(&m, 30, &sz));
    TEST_ASSERT_EQ_SIZE(10, sz);
    TEST_ASSERT_EQ_INT(MRT_ERR_NOT_FOUND, mrt_mem_block_at(&m, 31, &sz));

    /* ============ coalescing ============ */
    TEST_NAME("region-coalesce");
    mrt_mem_init(&m, 90, MRT_FIT_FIRST);
    size_t a = 999, b = 999, c = 999;
    mrt_mem_alloc(&m, 30, &a); /* [0,30)  */
    mrt_mem_alloc(&m, 30, &b); /* [30,60) */
    mrt_mem_alloc(&m, 30, &c); /* [60,90) */
    /* free b: hole at [30,60) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_free(&m, 30));
    TEST_ASSERT_EQ_SIZE(30, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(30, mrt_mem_largest_free(&m));
    TEST_ASSERT_EQ_SIZE(0, mrt_mem_fragmentation(&m));
    /* free a: coalesce right -> [0,60) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_free(&m, 0));
    TEST_ASSERT_EQ_SIZE(60, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(60, mrt_mem_largest_free(&m));
    /* free c: coalesce left+right -> [0,90) single free block */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_free(&m, 60));
    TEST_ASSERT_EQ_SIZE(90, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(1, (size_t)m.block_count);
    TEST_ASSERT_EQ_SIZE(90, mrt_mem_largest_free(&m));
    TEST_ASSERT_EQ_SIZE(0, mrt_mem_fragmentation(&m));

    /* ============ best fit vs first fit ============ */
    /* Non-adjacent holes are required (adjacent free blocks coalesce), so
     * keep a small allocated separator between them. */
    TEST_NAME("region-best-fit");
    size_t e = 999, d = 999, f2 = 999;
    mrt_mem_init(&m, 100, MRT_FIT_BEST);
    mrt_mem_alloc(&m, 10, &a); /* [0,10)  keep */
    mrt_mem_alloc(&m, 10, &b); /* [10,20) -> free (small hole) */
    mrt_mem_alloc(&m, 10, &c); /* [20,30) keep — separator */
    mrt_mem_alloc(&m, 40, &d); /* [30,70) -> free (big hole) */
    mrt_mem_alloc(&m, 30, &e); /* [70,100) keep */
    mrt_mem_free(&m, 10);
    mrt_mem_free(&m, 30);
    /* holes: [10,20) size 10 and [30,70) size 40, non-adjacent */
    TEST_ASSERT_EQ_SIZE(50, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(40, mrt_mem_largest_free(&m));
    /* request 35: only the 40-hole fits; best-fit takes it (first would too) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 35, &f2));
    TEST_ASSERT_EQ_SIZE(30, f2);
    /* request 5: best fit takes the 10-hole, NOT the 5-byte remainder */
    size_t g2 = 999;
    mrt_mem_init(&m, 100, MRT_FIT_BEST);
    mrt_mem_alloc(&m, 10, &a); /* [0,10) keep */
    mrt_mem_alloc(&m, 10, &b); /* [10,20) hole (10) */
    mrt_mem_alloc(&m, 10, &c); /* [20,30) separator */
    mrt_mem_alloc(&m, 40, &d); /* [30,70) hole (40) */
    mrt_mem_alloc(&m, 30, &e); /* [70,100) keep */
    mrt_mem_free(&m, 10);
    mrt_mem_free(&m, 30);
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 5, &g2));
    TEST_ASSERT_EQ_SIZE(10, g2); /* best fit -> smallest fitting hole */

    /* ============ worst fit ============ */
    TEST_NAME("region-worst-fit");
    mrt_mem_init(&m, 100, MRT_FIT_WORST);
    mrt_mem_alloc(&m, 10, &a); /* [0,10) keep */
    mrt_mem_alloc(&m, 10, &b); /* [10,20) hole (10) */
    mrt_mem_alloc(&m, 10, &c); /* [20,30) separator */
    mrt_mem_alloc(&m, 40, &d); /* [30,70) hole (40) */
    size_t f = 999;
    mrt_mem_alloc(&m, 30, &f); /* [70,100) keep */
    mrt_mem_free(&m, 10);
    mrt_mem_free(&m, 30);
    /* request 5: worst fit takes the 40-hole (not the 10-hole) */
    TEST_ASSERT_EQ_INT(MRT_OK, mrt_mem_alloc(&m, 5, &g2));
    TEST_ASSERT_EQ_SIZE(30, g2); /* worst -> big hole start */

    /* ============ fragmentation metric ============ */
    TEST_NAME("region-fragmentation");
    mrt_mem_init(&m, 120, MRT_FIT_FIRST);
    mrt_mem_alloc(&m, 40, &a);
    mrt_mem_alloc(&m, 40, &b);
    mrt_mem_alloc(&m, 40, &c);
    mrt_mem_free(&m, 0);
    mrt_mem_free(&m, 80);
    /* two 40-holes: free 80, largest 40 -> frag 40 */
    TEST_ASSERT_EQ_SIZE(80, mrt_mem_free_total(&m));
    TEST_ASSERT_EQ_SIZE(40, mrt_mem_largest_free(&m));
    TEST_ASSERT_EQ_SIZE(40, mrt_mem_fragmentation(&m));

    /* blocks listing */
    const mrt_mem_block_t *blocks = NULL;
    uint32_t nb = mrt_mem_blocks(&m, &blocks);
    TEST_ASSERT_EQ_INT(3, (int)nb); /* hole, used, hole */
    TEST_ASSERT_EQ_INT(0, (int)blocks[0].used);
    TEST_ASSERT_EQ_INT(1, (int)blocks[1].used);
    TEST_ASSERT_EQ_INT(0, (int)blocks[2].used);
    TEST_ASSERT_EQ_INT(0, (int)mrt_mem_blocks(NULL, &blocks));
    TEST_ASSERT_EQ_INT(0, (int)mrt_mem_blocks(&m, NULL));

    /* zero-size region degrades gracefully */
    mrt_mem_init(&m, 0, MRT_FIT_FIRST);
    TEST_ASSERT_EQ_INT(0, (int)m.block_count);
    TEST_ASSERT_EQ_INT(MRT_ERR_NO_MEMORY, mrt_mem_alloc(&m, 1, &a));

    return TEST_SUMMARY();
}
