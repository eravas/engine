/*
 * arena.h - bump allocator over caller-supplied memory. CONTRACT ONLY.
 *
 * The implementation (kernel/arena.c) is intentionally absent. The contract
 * below is specified by tests/todo/test_arena.c.
 *
 * An arena hands out consecutive pieces of one fixed region. Individual
 * allocations are never freed; the arena is rewound to a mark or reset as a
 * whole. Intended uses: a permanent arena, a per-map arena, and a per-frame
 * arena reset every tick.
 *
 * Not thread-safe: one arena belongs to one task.
 */
#ifndef KERNEL_ARENA_H
#define KERNEL_ARENA_H

#include <stddef.h>
#include <stdint.h>

/*
 * The fields are the implementation's business and may be reshaped freely;
 * callers and tests use only the functions. The struct is public so that it
 * can live in static storage or inside another arena.
 */
typedef struct arena {
    uint8_t *base;
    size_t   capacity;
    size_t   used;
    size_t   high_water;
} arena_t;

/* An opaque position within an arena, as returned by arena_mark(). */
typedef size_t arena_mark_t;

/*
 * Bind `a` to the region [memory, memory + size). The arena starts empty
 * with a high-water mark of zero. `memory` needs no particular alignment.
 * size == 0 is valid and yields an arena on which every allocation fails.
 * The region is not touched.
 */
void arena_init(arena_t *a, void *memory, size_t size);

/*
 * Allocate `size` bytes whose address is a multiple of `align`.
 *
 * Preconditions: align is a power of two (1 is allowed).
 *
 * On success returns a pointer inside the arena's region; the bytes are
 * zero-filled. Successive allocations never overlap and have increasing
 * addresses. Padding inserted to satisfy `align` counts as used.
 *
 * Returns NULL and leaves the arena completely unchanged (used and
 * high-water alike) if the request does not fit, including when
 * size + padding would overflow size_t. size == 0 always returns NULL and
 * changes nothing.
 */
void *arena_alloc(arena_t *a, size_t size, size_t align);

/* Current position. Valid until the arena is rewound to an earlier mark. */
arena_mark_t arena_mark(const arena_t *a);

/*
 * Rewind to `mark`, releasing everything allocated since arena_mark()
 * returned it. Precondition: `mark` came from this arena and nothing earlier
 * than it has been rewound since. Rewinding to the current position is a
 * no-op. The next allocation reuses the released bytes (zero-filled again).
 * The high-water mark is not lowered.
 */
void arena_rewind(arena_t *a, arena_mark_t mark);

/* Release everything. Equivalent to rewinding to the mark of an empty arena. */
void arena_reset(arena_t *a);

/* Bytes currently consumed, including alignment padding. */
size_t arena_used(const arena_t *a);

/* capacity - used. An allocation with align 1 of this size succeeds. */
size_t arena_remaining(const arena_t *a);

/*
 * The largest value arena_used() has ever had since arena_init(). Survives
 * rewind and reset; this is what the memory overlay displays.
 */
size_t arena_high_water(const arena_t *a);

#endif /* KERNEL_ARENA_H */
