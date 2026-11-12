/*
 * platform_rp2350.c - RP2350 backend. COMPILE-ONLY STUB.
 *
 * This file exists so that the portable code is compiled and linked for the
 * real floor target from the first commit. It has never run on hardware.
 * Only the clock is real; every device function is a stub that satisfies
 * the letter of platform.h:
 *
 *   present  discards the frame
 *   input    reports no buttons held, never Quit
 *   time     the SDK's 64-bit microsecond timer
 *   audio    returns false (no output), as the contract allows
 *   storage  every slot reads as empty; writes fail
 */
#include "pico/stdlib.h"
#include "pico/time.h"

#include "kernel/platform.h"

void platform_present(const uint8_t *pixels, const uint32_t *palette)
{
    (void)pixels;
    (void)palette;
}

void platform_poll_input(platform_input_t *out) { out->buttons = 0; }

uint64_t platform_time_us(void) { return time_us_64(); }

bool platform_audio_set_callback(platform_audio_fn fn, void *user)
{
    (void)user;
    return fn == NULL;
}

int32_t platform_storage_read(uint32_t slot, void *buf, uint32_t capacity)
{
    (void)buf;
    (void)capacity;
    return slot < PLATFORM_STORAGE_SLOTS ? PLATFORM_STORAGE_EMPTY
                                         : PLATFORM_STORAGE_ERROR;
}

bool platform_storage_write(uint32_t slot, const void *data, uint32_t size)
{
    (void)slot;
    (void)data;
    (void)size;
    return false;
}

int main(void)
{
    (void)app_main();
    for (;;) {
        tight_loop_contents();
    }
}
