/*
 * test_arena.c - executable specification of kernel/arena.h.
 *
 * Label: todo. This does not link until kernel/arena.c exists, and CI does
 * not fail on it. Each test names one clause of the contract.
 */
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/arena.h"
#include "kernel/memory.h"
#include "tests/support/test.h"

/* 64-byte alignment so that the padding arithmetic below is exact. */
static alignas(64) uint8_t s_mem[4096];

static bool all_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

static void init_gives_an_empty_arena(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    CHECK_EQ_U(arena_used(&a), 0);
    CHECK_EQ_U(arena_remaining(&a), sizeof s_mem);
    CHECK_EQ_U(arena_high_water(&a), 0);
}

static void init_does_not_touch_the_region(void)
{
    arena_t a;
    memset(s_mem, 0xA5, sizeof s_mem);
    arena_init(&a, s_mem, sizeof s_mem);
    for (size_t i = 0; i < sizeof s_mem; i++) {
        if (s_mem[i] != 0xA5) {
            CHECK(!"arena_init wrote to the region");
            break;
        }
    }
}

static void alloc_returns_zeroed_memory_inside_the_region(void)
{
    arena_t a;
    memset(s_mem, 0xA5, sizeof s_mem);
    arena_init(&a, s_mem, sizeof s_mem);

    uint8_t *p = arena_alloc(&a, 100, 1);
    REQUIRE(p != NULL);
    CHECK(p >= s_mem && p + 100 <= s_mem + sizeof s_mem);
    CHECK(all_zero(p, 100));
    CHECK_EQ_U(arena_used(&a), 100);
    CHECK_EQ_U(arena_remaining(&a), sizeof s_mem - 100);
}

static void allocations_do_not_overlap_and_ascend(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    uint8_t *p = arena_alloc(&a, 10, 1);
    uint8_t *q = arena_alloc(&a, 20, 1);
    uint8_t *r = arena_alloc(&a, 30, 1);
    REQUIRE(p != NULL && q != NULL && r != NULL);
    CHECK(p + 10 <= q);
    CHECK(q + 20 <= r);
}

static void alignment_is_honoured_and_padding_is_counted(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);

    REQUIRE(arena_alloc(&a, 1, 1) != NULL);
    for (size_t align = 1; align <= 64; align *= 2) {
        uint8_t *p = arena_alloc(&a, 3, align);
        REQUIRE(p != NULL);
        CHECK_EQ_U((uintptr_t)p % align, 0);
        /* used covers everything up to the end of this allocation */
        CHECK_EQ_U(arena_used(&a), (size_t)(p + 3 - s_mem));
    }
}

static void alignment_is_of_the_address_not_the_offset(void)
{
    /* Start the region at an odd address. */
    arena_t a;
    arena_init(&a, s_mem + 1, sizeof s_mem - 1);
    uint8_t *p = arena_alloc(&a, 8, 16);
    REQUIRE(p != NULL);
    CHECK_EQ_U((uintptr_t)p % 16, 0);
    CHECK(p >= s_mem + 1);
}

static void exact_fit_succeeds_and_one_more_byte_fails(void)
{
    arena_t a;
    arena_init(&a, s_mem, 64);
    CHECK(arena_alloc(&a, 64, 1) != NULL);
    CHECK_EQ_U(arena_remaining(&a), 0);
    CHECK(arena_alloc(&a, 1, 1) == NULL);

    arena_init(&a, s_mem, 64);
    CHECK(arena_alloc(&a, 65, 1) == NULL);
}

static void failed_alloc_changes_nothing(void)
{
    arena_t a;
    arena_init(&a, s_mem, 64);
    REQUIRE(arena_alloc(&a, 10, 1) != NULL);

    CHECK(arena_alloc(&a, 1000, 1) == NULL);
    CHECK_EQ_U(arena_used(&a), 10);
    CHECK_EQ_U(arena_high_water(&a), 10);

    /* Fits by size but not once padding is added. */
    arena_init(&a, s_mem, 40);
    REQUIRE(arena_alloc(&a, 1, 1) != NULL);
    CHECK(arena_alloc(&a, 16, 32) == NULL);
    CHECK_EQ_U(arena_used(&a), 1);

    /* Still usable afterwards. */
    CHECK(arena_alloc(&a, 39, 1) != NULL);
}

static void zero_size_returns_null_and_changes_nothing(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    REQUIRE(arena_alloc(&a, 5, 1) != NULL);
    CHECK(arena_alloc(&a, 0, 1) == NULL);
    CHECK(arena_alloc(&a, 0, 64) == NULL);
    CHECK_EQ_U(arena_used(&a), 5);
}

static void huge_sizes_fail_without_wrapping(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    REQUIRE(arena_alloc(&a, 3, 1) != NULL);
    CHECK(arena_alloc(&a, SIZE_MAX, 1) == NULL);
    CHECK(arena_alloc(&a, SIZE_MAX - 2, 1) == NULL);
    CHECK(arena_alloc(&a, SIZE_MAX - 8, 16) == NULL);
    CHECK_EQ_U(arena_used(&a), 3);
}

static void zero_capacity_arena_always_fails(void)
{
    arena_t a;
    arena_init(&a, s_mem, 0);
    CHECK_EQ_U(arena_remaining(&a), 0);
    CHECK(arena_alloc(&a, 1, 1) == NULL);
    arena_reset(&a);
    CHECK_EQ_U(arena_used(&a), 0);
}

static void rewind_releases_back_to_the_mark(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    uint8_t *keep = arena_alloc(&a, 16, 1);
    REQUIRE(keep != NULL);
    memset(keep, 0x11, 16);

    arena_mark_t mark = arena_mark(&a);
    uint8_t *tmp = arena_alloc(&a, 32, 1);
    REQUIRE(tmp != NULL);
    memset(tmp, 0x22, 32);
    CHECK_EQ_U(arena_used(&a), 48);

    arena_rewind(&a, mark);
    CHECK_EQ_U(arena_used(&a), 16);

    /* Earlier allocations are untouched. */
    for (int i = 0; i < 16; i++) {
        if (keep[i] != 0x11) {
            CHECK(!"rewind disturbed an earlier allocation");
            break;
        }
    }

    /* Released bytes are reused, and come back zeroed. */
    uint8_t *again = arena_alloc(&a, 32, 1);
    CHECK(again == tmp);
    REQUIRE(again != NULL);
    CHECK(all_zero(again, 32));
}

static void rewind_to_current_position_is_a_no_op(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    REQUIRE(arena_alloc(&a, 7, 1) != NULL);
    arena_rewind(&a, arena_mark(&a));
    CHECK_EQ_U(arena_used(&a), 7);
}

static void marks_nest(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    arena_mark_t outer = arena_mark(&a);
    REQUIRE(arena_alloc(&a, 10, 1) != NULL);
    arena_mark_t inner = arena_mark(&a);
    REQUIRE(arena_alloc(&a, 20, 1) != NULL);

    arena_rewind(&a, inner);
    CHECK_EQ_U(arena_used(&a), 10);
    arena_rewind(&a, outer);
    CHECK_EQ_U(arena_used(&a), 0);
}

static void reset_empties_the_arena(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    uint8_t *first = arena_alloc(&a, 100, 1);
    REQUIRE(first != NULL);
    memset(first, 0xFF, 100);
    REQUIRE(arena_alloc(&a, 200, 8) != NULL);

    arena_reset(&a);
    CHECK_EQ_U(arena_used(&a), 0);
    CHECK_EQ_U(arena_remaining(&a), sizeof s_mem);

    uint8_t *again = arena_alloc(&a, 100, 1);
    CHECK(again == first);
    REQUIRE(again != NULL);
    CHECK(all_zero(again, 100));
}

static void high_water_survives_rewind_and_reset(void)
{
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    arena_mark_t mark = arena_mark(&a);
    REQUIRE(arena_alloc(&a, 300, 1) != NULL);
    CHECK_EQ_U(arena_high_water(&a), 300);

    arena_rewind(&a, mark);
    CHECK_EQ_U(arena_high_water(&a), 300);

    REQUIRE(arena_alloc(&a, 100, 1) != NULL);
    CHECK_EQ_U(arena_high_water(&a), 300);

    REQUIRE(arena_alloc(&a, 250, 1) != NULL);
    CHECK_EQ_U(arena_high_water(&a), 350);

    arena_reset(&a);
    CHECK_EQ_U(arena_high_water(&a), 350);

    /* Only a fresh init clears it. */
    arena_init(&a, s_mem, sizeof s_mem);
    CHECK_EQ_U(arena_high_water(&a), 0);
}

static void per_frame_pattern_never_grows(void)
{
    /* A frame arena reset every tick must reach a steady state. */
    arena_t a;
    arena_init(&a, s_mem, sizeof s_mem);
    for (int frame = 0; frame < 1000; frame++) {
        arena_reset(&a);
        REQUIRE(arena_alloc(&a, 1000, 4) != NULL);
        REQUIRE(arena_alloc(&a, 2000, 8) != NULL);
    }
    CHECK(arena_high_water(&a) <= 3008);
}

static void the_whole_memory_block_can_be_one_arena(void)
{
    static alignas(max_align_t) uint8_t block[MEMORY_BLOCK_SIZE];
    arena_t a;
    arena_init(&a, block, sizeof block);
    CHECK(arena_alloc(&a, PLATFORM_FB_SIZE, 4) != NULL);
    CHECK(arena_alloc(&a, MEMORY_BLOCK_SIZE - PLATFORM_FB_SIZE, 1) != NULL);
    CHECK_EQ_U(arena_remaining(&a), 0);
    CHECK(arena_alloc(&a, 1, 1) == NULL);
}

TEST_MAIN(
    TEST(init_gives_an_empty_arena),
    TEST(init_does_not_touch_the_region),
    TEST(alloc_returns_zeroed_memory_inside_the_region),
    TEST(allocations_do_not_overlap_and_ascend),
    TEST(alignment_is_honoured_and_padding_is_counted),
    TEST(alignment_is_of_the_address_not_the_offset),
    TEST(exact_fit_succeeds_and_one_more_byte_fails),
    TEST(failed_alloc_changes_nothing),
    TEST(zero_size_returns_null_and_changes_nothing),
    TEST(huge_sizes_fail_without_wrapping),
    TEST(zero_capacity_arena_always_fails),
    TEST(rewind_releases_back_to_the_mark),
    TEST(rewind_to_current_position_is_a_no_op),
    TEST(marks_nest),
    TEST(reset_empties_the_arena),
    TEST(high_water_survives_rewind_and_reset),
    TEST(per_frame_pattern_never_grows),
    TEST(the_whole_memory_block_can_be_one_arena))
