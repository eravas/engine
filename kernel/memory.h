/*
 * memory.h - the engine's memory budget.
 *
 * All engine memory comes from one static block of MEMORY_BLOCK_SIZE bytes.
 * After start-up nothing is taken from a heap: the block is carved into
 * arenas (arena.h) and pools (pool.h) once, and those are the only
 * allocators engine code may use. malloc and free are forbidden outside
 * platform backends (tests/lint enforces this for kernel/, sim/ and game/).
 *
 * The host build uses the same constant so that it cannot fit something the
 * RP2350 floor (520 KB of on-chip RAM) could not.
 *
 * This header deliberately declares no storage and no functions. Where the
 * block is defined and how it is split is part of the allocator work.
 */
#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include "kernel/platform.h"

#define MEMORY_BLOCK_SIZE (512u * 1024u)

/* The framebuffer alone must leave room for everything else. */
_Static_assert(PLATFORM_FB_SIZE < MEMORY_BLOCK_SIZE,
               "framebuffer does not fit the memory block");

#endif /* KERNEL_MEMORY_H */
