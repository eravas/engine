/*
 * smoke_app.c - exercises every platform.h function through app_main().
 *
 * This is a platform test fixture, not engine code: it has no fixed
 * timestep, no arenas and no renderer, and its pattern generator is not a
 * blitter. It is written under the engine's rules (integers only, no libc
 * beyond <string.h>, platform.h only) so that the same source links against
 * all three backends.
 *
 * It presents a moving test pattern with a strip showing the held buttons,
 * plays a quiet tone, and bumps a run counter in storage slot 0. It returns
 * when the platform raises PLATFORM_BTN_QUIT.
 */
#include <string.h>

#include "kernel/platform.h"

static uint8_t  s_pixels[PLATFORM_FB_SIZE];
static uint32_t s_palette[PLATFORM_PALETTE_SIZE];

static void tone(void *user, int16_t *frames, uint32_t frame_count)
{
    uint32_t *phase = user;
    for (uint32_t i = 0; i < frame_count; i++) {
        /* 22050 / 50 = 441 Hz square wave, low volume. */
        frames[i] = ((*phase)++ / 25u) % 2u != 0 ? 1200 : -1200;
    }
}

static void draw(uint32_t frame, uint32_t buttons)
{
    for (uint32_t y = 0; y < PLATFORM_FB_HEIGHT; y++) {
        uint8_t *row = s_pixels + y * PLATFORM_FB_WIDTH;
        for (uint32_t x = 0; x < PLATFORM_FB_WIDTH; x++) {
            row[x] = (uint8_t)((x ^ y) + frame);
        }
    }
    /* One 16x8 cell per button bit along the top edge. */
    for (uint32_t bit = 0; bit < 10; bit++) {
        uint8_t colour = (buttons >> bit) & 1u ? 255 : 0;
        for (uint32_t y = 4; y < 12; y++) {
            memset(s_pixels + y * PLATFORM_FB_WIDTH + 4 + bit * 20, colour, 16);
        }
    }
}

int app_main(void)
{
    /* Greyscale ramp with a tint, index 255 forced to pure white. */
    for (uint32_t i = 0; i < PLATFORM_PALETTE_SIZE; i++) {
        s_palette[i] = (i << 16) | ((i / 2u) << 8) | (255u - i);
    }
    s_palette[255] = 0x00FFFFFFu;

    uint32_t runs = 0;
    if (platform_storage_read(0, &runs, sizeof runs) != (int32_t)sizeof runs) {
        runs = 0;
    }
    runs++;
    (void)platform_storage_write(0, &runs, sizeof runs);

    static uint32_t phase;
    (void)platform_audio_set_callback(tone, &phase);

    uint64_t last = platform_time_us();
    int status = 0;
    for (uint32_t frame = 0;; frame++) {
        platform_input_t in;
        platform_poll_input(&in);
        if (in.buttons & PLATFORM_BTN_QUIT) {
            break;
        }
        uint64_t now = platform_time_us();
        if (now < last) {
            status = 1; /* the clock went backwards */
        }
        last = now;

        draw(frame, in.buttons);
        platform_present(s_pixels, s_palette);
    }

    (void)platform_audio_set_callback(NULL, NULL);
    return status;
}
