/*
 * pool.h - fixed-capacity object pool addressed by generation handles.
 * CONTRACT ONLY.
 *
 * The implementation (kernel/pool.c) is intentionally absent. The contract
 * below is specified by tests/todo/test_pool.c.
 *
 * A pool holds up to `capacity` items of one size. Items are referred to by
 * handle, never by stored pointer: a handle names a slot plus the generation
 * the slot had when the item was allocated, so a handle to a freed item is
 * detected instead of silently reaching whatever reused the slot.
 *
 * Not thread-safe: one pool belongs to one task.
 */
#ifndef KERNEL_POOL_H
#define KERNEL_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A handle is an opaque 32-bit value. Only HANDLE_NULL has a defined bit
 * pattern. How slot index and generation are packed into the rest is the
 * implementation's choice; callers must not decode handles.
 *
 * Handles are plain data: they may be copied, stored in other pool items and
 * written to a save file. A handle is only meaningful to the pool that
 * issued it.
 */
typedef uint32_t handle_t;
#define HANDLE_NULL ((handle_t)0)

#define POOL_MAX_CAPACITY 65535u

/*
 * The fields are the implementation's business and may be reshaped freely;
 * callers and tests use only the functions.
 */
typedef struct pool {
    void    *memory;
    uint32_t item_size;
    uint32_t capacity;
    uint32_t live;
    uint32_t high_water;
    uint32_t free_head;
} pool_t;

/*
 * Bytes of backing memory pool_init() needs for this shape, including all
 * bookkeeping. Returns 0 if the shape is invalid (item_size == 0,
 * capacity == 0, capacity > POOL_MAX_CAPACITY, or the size overflows).
 * A pure function of its arguments.
 */
size_t pool_required_bytes(uint32_t item_size, uint32_t capacity);

/*
 * Bind `p` to `memory` and make every slot free.
 *
 * Preconditions: `memory` is aligned to alignof(max_align_t) and stays valid
 * for the pool's lifetime (typically it comes from an arena).
 *
 * Returns false, leaving `p` unusable, if the shape is invalid or
 * memory_size < pool_required_bytes(item_size, capacity). Otherwise returns
 * true with live count and high-water mark at zero.
 */
bool pool_init(pool_t *p, void *memory, size_t memory_size,
               uint32_t item_size, uint32_t capacity);

/*
 * Take a free slot. Returns its handle, or HANDLE_NULL if all `capacity`
 * slots are live (the pool is then unchanged).
 *
 * The new item is zero-filled. Its handle differs from HANDLE_NULL, from
 * every other live handle, and from the handle of the item that last
 * occupied the same slot.
 *
 * Which slot is chosen, and therefore the handle value, must be a
 * deterministic function of the sequence of pool_alloc/pool_free calls since
 * pool_init: two pools of the same shape driven by the same sequence issue
 * identical handles. Nothing may depend on addresses.
 */
handle_t pool_alloc(pool_t *p);

/*
 * Resolve a handle. Returns a pointer to the item, aligned to
 * alignof(max_align_t), or NULL if `h` is HANDLE_NULL, was never issued by
 * this pool, or names an item that has since been freed (even if its slot
 * has been reused).
 *
 * The pointer is valid until that item is freed. It must not be kept across
 * ticks; keep the handle.
 */
void *pool_get(const pool_t *p, handle_t h);

/*
 * Free the item named by `h`. Returns true if it was live. Returns false and
 * changes nothing for any handle pool_get() would reject, so a double free
 * is harmless.
 *
 * Staleness limit: a slot's generation is finite. After a slot has been
 * reused 2^16 - 1 times a very old handle to it may be accepted again. Every
 * handle short of that age is rejected reliably, and a slot must remain
 * usable however often it is recycled.
 */
bool pool_free(pool_t *p, handle_t h);

/* Number of live items. */
uint32_t pool_live(const pool_t *p);

/* The capacity given to pool_init(). */
uint32_t pool_capacity(const pool_t *p);

/* The largest value pool_live() has had since pool_init(). */
uint32_t pool_high_water(const pool_t *p);

#endif /* KERNEL_POOL_H */
