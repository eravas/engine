/*
 * budget_app.c - proves that a full engine block fits the target's RAM.
 *
 * kernel/memory.h only names a size. Until the allocator work defines the
 * real block, this fixture stands in for it: one static object of exactly
 * MEMORY_BLOCK_SIZE bytes, linked together with a platform backend. On the
 * rp2350 preset the link fails if the block and the platform's own data no
 * longer fit main RAM together, and the ram_budget test reports the margin.
 *
 * Everything this app uses lives inside the block, as engine code's memory
 * will: the framebuffer first, the palette after it. It is a test fixture,
 * not an allocator.
 *
 * When the kernel defines the real block, link that here instead.
 */
#include <string.h>

#include "kernel/memory.h"

/* Not static: tests/budget/check_ram_budget.cmake looks the symbol up. */
uint32_t g_engine_block[MEMORY_BLOCK_SIZE / sizeof(uint32_t)];

_Static_assert(sizeof g_engine_block == MEMORY_BLOCK_SIZE,
               "block size must be a multiple of 4");

int app_main(void)
{
    uint8_t  *pixels = (uint8_t *)g_engine_block;
    uint32_t *palette = g_engine_block + PLATFORM_FB_SIZE / sizeof(uint32_t);

    /* Touch the first and last byte so that the whole block is live. */
    volatile uint8_t *bytes = (volatile uint8_t *)g_engine_block;
    bytes[0] = 1;
    bytes[MEMORY_BLOCK_SIZE - 1] = 1;

    for (uint32_t i = 0; i < PLATFORM_PALETTE_SIZE; i++) {
        palette[i] = i * 0x010101u;
    }
    for (;;) {
        platform_input_t in;
        platform_poll_input(&in);
        if (in.buttons & PLATFORM_BTN_QUIT) {
            break;
        }
        memset(pixels, (int)(in.buttons & 0xFFu), PLATFORM_FB_SIZE);
        platform_present(pixels, palette);
    }
    return bytes[MEMORY_BLOCK_SIZE - 1] == 1 ? 0 : 1;
}
