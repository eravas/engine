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
 * RP2350 floor could not.
 *
 * Where the size comes from
 * -------------------------
 * The RP2350 has 520 KiB of on-chip SRAM, but not as one piece. The Pico
 * SDK's default memory map splits it into:
 *
 *   main RAM    512 KiB   every static object, and the C heap
 *   SCRATCH_X     4 KiB   core 1 stack
 *   SCRATCH_Y     4 KiB   core 0 stack
 *
 * So the engine block shares the 512 KiB main region with everything the
 * platform needs there. A 512 KiB block cannot link: with the stub backend
 * the region overflows by 3216 bytes.
 *
 * MEMORY_PLATFORM_RESERVE is the part of main RAM set aside for everything
 * that is not the engine block:
 *
 *   measured   3.2 KiB  SDK with the stub backend: vector table 272 B,
 *                       .data 380 B, .bss 508 B, minimum heap 2048 B
 *   estimate    12 KiB  a real backend: display transfer buffers, audio
 *                       ring buffer, a 4 KiB flash sector buffer, stdio
 *   estimate    16 KiB  room to move both stacks into main RAM at 8 KiB
 *                       each, if 4 KiB per core in the scratch banks proves
 *                       too small
 *   --------   -------
 *   reserve     32 KiB  (rounded up)
 *
 * Only the first line is measured. The reserve is checked on every build of
 * the rp2350 preset: tests/budget links a real block of MEMORY_BLOCK_SIZE
 * for the target, so the build fails if block plus platform no longer fit,
 * and the ram_budget test reports how much of the reserve is in use.
 * Revisit the split when a real backend exists; changing it is one constant.
 *
 * This header deliberately declares no storage and no functions. Where the
 * block is defined and how it is split is part of the allocator work.
 */
#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include "kernel/platform.h"

/* The RP2350 region that holds all static data (see above). */
#define MEMORY_MAIN_RAM_SIZE     (512u * 1024u)

/* Main RAM kept for the platform: SDK, backend, heap, stack headroom. */
#define MEMORY_PLATFORM_RESERVE  (32u * 1024u)

/* The engine's one static block: 480 KiB. */
#define MEMORY_BLOCK_SIZE        (MEMORY_MAIN_RAM_SIZE - MEMORY_PLATFORM_RESERVE)

/* The framebuffer alone must leave room for everything else. */
_Static_assert(PLATFORM_FB_SIZE < MEMORY_BLOCK_SIZE,
               "framebuffer does not fit the memory block");

#endif /* KERNEL_MEMORY_H */
