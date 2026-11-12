/*
 * test_pool.c - executable specification of kernel/pool.h.
 *
 * Label: todo. This does not link until kernel/pool.c exists, and CI does
 * not fail on it. The tests treat handles as opaque: nothing here assumes
 * how index and generation are packed.
 */
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/pool.h"
#include "tests/support/test.h"

typedef struct item {
    uint32_t a;
    uint16_t b;
    uint8_t  c[10];
} item_t;

#define CAP 8u

static alignas(max_align_t) uint8_t s_mem[16384];
static alignas(max_align_t) uint8_t s_mem2[16384];

static void make(pool_t *p, void *mem, uint32_t capacity)
{
    REQUIRE(pool_required_bytes(sizeof(item_t), capacity) <= sizeof s_mem);
    REQUIRE(pool_init(p, mem, sizeof s_mem, sizeof(item_t), capacity));
}

static void required_bytes_rejects_invalid_shapes(void)
{
    CHECK_EQ_U(pool_required_bytes(0, 4), 0);
    CHECK_EQ_U(pool_required_bytes(16, 0), 0);
    CHECK_EQ_U(pool_required_bytes(16, POOL_MAX_CAPACITY + 1u), 0);

    /* A shape whose size overflows size_t on a 32-bit target: it must be
     * either rejected or reported honestly, never wrapped to a small size. */
    size_t huge = pool_required_bytes(UINT32_MAX, POOL_MAX_CAPACITY);
    CHECK(huge == 0 || huge >= (size_t)UINT32_MAX);
}

static void required_bytes_covers_the_items(void)
{
    size_t n = pool_required_bytes(sizeof(item_t), CAP);
    CHECK(n >= sizeof(item_t) * CAP);
    CHECK(pool_required_bytes(sizeof(item_t), CAP * 2) > n);
    CHECK_EQ_U(pool_required_bytes(sizeof(item_t), CAP), n); /* pure */
    CHECK(pool_required_bytes(1, POOL_MAX_CAPACITY) != 0);
}

static void init_rejects_bad_shape_or_short_memory(void)
{
    pool_t p;
    size_t need = pool_required_bytes(sizeof(item_t), CAP);
    REQUIRE(need != 0 && need <= sizeof s_mem);

    CHECK(!pool_init(&p, s_mem, need - 1, sizeof(item_t), CAP));
    CHECK(!pool_init(&p, s_mem, sizeof s_mem, 0, CAP));
    CHECK(!pool_init(&p, s_mem, sizeof s_mem, sizeof(item_t), 0));
    CHECK(pool_init(&p, s_mem, need, sizeof(item_t), CAP)); /* exact fit */
}

static void init_gives_an_empty_pool(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    CHECK_EQ_U(pool_live(&p), 0);
    CHECK_EQ_U(pool_capacity(&p), CAP);
    CHECK_EQ_U(pool_high_water(&p), 0);
}

static void alloc_gives_distinct_live_handles_until_full(void)
{
    pool_t p;
    handle_t h[CAP];
    make(&p, s_mem, CAP);

    for (uint32_t i = 0; i < CAP; i++) {
        h[i] = pool_alloc(&p);
        CHECK(h[i] != HANDLE_NULL);
        for (uint32_t j = 0; j < i; j++) {
            CHECK(h[i] != h[j]);
        }
        CHECK_EQ_U(pool_live(&p), i + 1);
    }

    /* Full: fails and changes nothing. */
    CHECK_EQ_U(pool_alloc(&p), HANDLE_NULL);
    CHECK_EQ_U(pool_live(&p), CAP);
    for (uint32_t i = 0; i < CAP; i++) {
        CHECK(pool_get(&p, h[i]) != NULL);
    }
}

static void items_are_zeroed_aligned_and_separate(void)
{
    pool_t p;
    item_t *items[CAP];
    memset(s_mem, 0xA5, sizeof s_mem);
    make(&p, s_mem, CAP);

    for (uint32_t i = 0; i < CAP; i++) {
        handle_t h = pool_alloc(&p);
        items[i] = pool_get(&p, h);
        REQUIRE(items[i] != NULL);
        CHECK_EQ_U((uintptr_t)items[i] % alignof(max_align_t), 0);
        CHECK((uint8_t *)items[i] >= s_mem &&
              (uint8_t *)(items[i] + 1) <= s_mem + sizeof s_mem);

        static const item_t zero;
        CHECK(memcmp(items[i], &zero, sizeof zero) == 0);
        memset(items[i], (int)(i + 1), sizeof(item_t));
    }
    /* Writing one item fully did not disturb any other. */
    for (uint32_t i = 0; i < CAP; i++) {
        const uint8_t *bytes = (const uint8_t *)items[i];
        for (size_t k = 0; k < sizeof(item_t); k++) {
            if (bytes[k] != i + 1) {
                CHECK(!"items overlap");
                return;
            }
        }
    }
}

static void get_is_stable_while_live(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    handle_t h1 = pool_alloc(&p);
    handle_t h2 = pool_alloc(&p);
    item_t *i1 = pool_get(&p, h1);
    REQUIRE(i1 != NULL);
    i1->a = 0xC0FFEEu;

    CHECK(pool_free(&p, h2));
    handle_t h3 = pool_alloc(&p);
    (void)h3;
    CHECK(pool_get(&p, h1) == i1);
    CHECK_EQ_U(i1->a, 0xC0FFEEu);
}

static void null_and_garbage_handles_are_rejected(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    handle_t h = pool_alloc(&p);
    REQUIRE(h != HANDLE_NULL);

    CHECK(pool_get(&p, HANDLE_NULL) == NULL);
    CHECK(!pool_free(&p, HANDLE_NULL));

    /* Every 32-bit pattern other than the one live handle must be refused. */
    static const handle_t probes[] = {
        1u,          0xFFFFu,     0x10000u,    0xFFFF0000u, 0xFFFFFFFFu,
        0x7FFFFFFFu, 0x80000000u, 0x00010001u, 0xDEADBEEFu, 0x12345678u,
    };
    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        if (probes[i] == h) {
            continue;
        }
        CHECK(pool_get(&p, probes[i]) == NULL);
        CHECK(!pool_free(&p, probes[i]));
    }
    CHECK_EQ_U(pool_live(&p), 1);
    CHECK(pool_get(&p, h) != NULL);
}

static void freed_handle_is_stale(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    handle_t h = pool_alloc(&p);
    CHECK(pool_free(&p, h));
    CHECK_EQ_U(pool_live(&p), 0);
    CHECK(pool_get(&p, h) == NULL);
}

static void double_free_is_refused_and_harmless(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    handle_t keep = pool_alloc(&p);
    handle_t h = pool_alloc(&p);
    CHECK(pool_free(&p, h));
    CHECK(!pool_free(&p, h));
    CHECK_EQ_U(pool_live(&p), 1);
    CHECK(pool_get(&p, keep) != NULL);

    /* The free list is intact: the pool still holds exactly CAP items. */
    uint32_t got = 0;
    while (pool_alloc(&p) != HANDLE_NULL) {
        got++;
        REQUIRE(got <= CAP);
    }
    CHECK_EQ_U(got, CAP - 1);
}

static void stale_handle_stays_stale_after_slot_reuse(void)
{
    /* Capacity 1 forces the slot to be reused. */
    pool_t p;
    make(&p, s_mem, 1);
    handle_t old = pool_alloc(&p);
    item_t *item = pool_get(&p, old);
    REQUIRE(item != NULL);
    item->a = 111;
    CHECK(pool_free(&p, old));

    handle_t fresh = pool_alloc(&p);
    REQUIRE(fresh != HANDLE_NULL);
    CHECK(fresh != old);
    CHECK(pool_get(&p, old) == NULL);
    CHECK(!pool_free(&p, old));

    item_t *again = pool_get(&p, fresh);
    REQUIRE(again != NULL);
    CHECK_EQ_U(again->a, 0); /* zeroed again on reuse */
    CHECK_EQ_U(pool_live(&p), 1);
}

static void freed_slots_can_all_be_reallocated(void)
{
    pool_t p;
    handle_t h[CAP];
    make(&p, s_mem, CAP);
    for (int round = 0; round < 50; round++) {
        for (uint32_t i = 0; i < CAP; i++) {
            h[i] = pool_alloc(&p);
            REQUIRE(h[i] != HANDLE_NULL);
        }
        CHECK_EQ_U(pool_alloc(&p), HANDLE_NULL);
        /* Free in a different order each round. */
        for (uint32_t i = 0; i < CAP; i++) {
            uint32_t k = (i * 3u + (uint32_t)round) % CAP;
            CHECK(pool_free(&p, h[k]));
        }
        CHECK_EQ_U(pool_live(&p), 0);
    }
}

static void a_slot_survives_generation_wraparound(void)
{
    /* 70000 > 2^16: the generation counter must wrap without ever issuing
     * HANDLE_NULL, retiring the slot, or accepting the previous handle. */
    pool_t p;
    make(&p, s_mem, 1);
    handle_t prev = HANDLE_NULL;
    for (uint32_t i = 0; i < 70000u; i++) {
        handle_t h = pool_alloc(&p);
        if (h == HANDLE_NULL || h == prev || pool_get(&p, h) == NULL ||
            (prev != HANDLE_NULL && pool_get(&p, prev) != NULL)) {
            CHECK_EQ_U(i, UINT32_MAX); /* report the failing iteration */
            return;
        }
        REQUIRE(pool_free(&p, h));
        prev = h;
    }
}

static void high_water_tracks_peak_live_count(void)
{
    pool_t p;
    make(&p, s_mem, CAP);
    handle_t a = pool_alloc(&p);
    handle_t b = pool_alloc(&p);
    handle_t c = pool_alloc(&p);
    CHECK_EQ_U(pool_high_water(&p), 3);
    pool_free(&p, a);
    pool_free(&p, b);
    CHECK_EQ_U(pool_high_water(&p), 3);
    (void)pool_alloc(&p);
    CHECK_EQ_U(pool_high_water(&p), 3);
    (void)pool_alloc(&p);
    (void)pool_alloc(&p);
    CHECK_EQ_U(pool_high_water(&p), 4);
    (void)c;

    make(&p, s_mem, CAP); /* re-init clears it */
    CHECK_EQ_U(pool_high_water(&p), 0);
}

static void handle_values_are_a_function_of_the_call_sequence(void)
{
    /* Two pools, different memory, same shape, same calls: same handles. */
    pool_t p, q;
    handle_t hp[CAP], hq[CAP];
    memset(s_mem, 0x00, sizeof s_mem);
    memset(s_mem2, 0xFF, sizeof s_mem2);
    make(&p, s_mem, CAP);
    make(&q, s_mem2, CAP);

    uint32_t lcg = 1;
    uint32_t live = 0;
    for (int step = 0; step < 2000; step++) {
        lcg = lcg * 1664525u + 1013904223u;
        bool do_alloc = live == 0 || (live < CAP && (lcg >> 16) % 2u == 0);
        if (do_alloc) {
            hp[live] = pool_alloc(&p);
            hq[live] = pool_alloc(&q);
            if (hp[live] != hq[live] || hp[live] == HANDLE_NULL) {
                CHECK_EQ_U(hp[live], hq[live]);
                CHECK(hp[live] != HANDLE_NULL);
                return;
            }
            live++;
        } else {
            uint32_t k = (lcg >> 8) % live;
            REQUIRE(pool_free(&p, hp[k]));
            REQUIRE(pool_free(&q, hq[k]));
            live--;
            hp[k] = hp[live];
            hq[k] = hq[live];
        }
    }
    CHECK_EQ_U(pool_live(&p), pool_live(&q));
}

static void maximum_capacity_works(void)
{
    static alignas(max_align_t) uint8_t big[4u * 1024u * 1024u];
    pool_t p;
    size_t need = pool_required_bytes(8, POOL_MAX_CAPACITY);
    REQUIRE(need != 0 && need <= sizeof big);
    REQUIRE(pool_init(&p, big, sizeof big, 8, POOL_MAX_CAPACITY));

    handle_t first = pool_alloc(&p);
    handle_t last = first;
    for (uint32_t i = 1; i < POOL_MAX_CAPACITY; i++) {
        last = pool_alloc(&p);
        if (last == HANDLE_NULL) {
            CHECK_EQ_U(i, POOL_MAX_CAPACITY); /* ran out early */
            return;
        }
    }
    CHECK_EQ_U(pool_live(&p), POOL_MAX_CAPACITY);
    CHECK_EQ_U(pool_alloc(&p), HANDLE_NULL);
    CHECK(pool_get(&p, first) != NULL);
    CHECK(pool_get(&p, last) != NULL);
    CHECK(pool_get(&p, first) != pool_get(&p, last));
}

TEST_MAIN(
    TEST(required_bytes_rejects_invalid_shapes),
    TEST(required_bytes_covers_the_items),
    TEST(init_rejects_bad_shape_or_short_memory),
    TEST(init_gives_an_empty_pool),
    TEST(alloc_gives_distinct_live_handles_until_full),
    TEST(items_are_zeroed_aligned_and_separate),
    TEST(get_is_stable_while_live),
    TEST(null_and_garbage_handles_are_rejected),
    TEST(freed_handle_is_stale),
    TEST(double_free_is_refused_and_harmless),
    TEST(stale_handle_stays_stale_after_slot_reuse),
    TEST(freed_slots_can_all_be_reallocated),
    TEST(a_slot_survives_generation_wraparound),
    TEST(high_water_tracks_peak_live_count),
    TEST(handle_values_are_a_function_of_the_call_sequence),
    TEST(maximum_capacity_works))
