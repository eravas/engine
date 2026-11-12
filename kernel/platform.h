/*
 * platform.h - the complete contract between portable engine code and a
 * platform backend.
 *
 * Everything above this header (kernel/, sim/, game/) is portable C11 and may
 * not touch SDL, the OS or a chip SDK. Everything below it lives in
 * platform/<backend>/ and implements exactly the functions declared here.
 *
 * General rules that hold for every function:
 *   - No function allocates on behalf of the caller or hands out memory the
 *     caller must free.
 *   - Pointers passed in are borrowed for the duration of the call only.
 *   - Unless stated otherwise, functions are called from the engine's main
 *     context and are not re-entrant.
 *   - No floating point crosses this interface.
 *
 * Task affinity (matters once simulation and rendering run on two cores):
 *   simulation task: platform_poll_input, platform_storage_*
 *   render task:     platform_present
 *   either task:     platform_time_us, platform_audio_set_callback
 * A single-core build calls all of them in turn from one context.
 */
#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* ---- Entry point ------------------------------------------------------- */

/*
 * Defined by the application (the game, or a test program), not the backend.
 *
 * The backend owns the real entry point (main, or the chip's reset path). It
 * initialises itself, calls app_main() exactly once, and shuts down when
 * app_main() returns. The return value becomes the process exit status on
 * hosted backends and is ignored on bare metal. app_main() may call any
 * function in this header from its first statement.
 */
int app_main(void);

/* ---- Display ----------------------------------------------------------- */

#define PLATFORM_FB_WIDTH    320
#define PLATFORM_FB_HEIGHT   240
#define PLATFORM_FB_SIZE     (PLATFORM_FB_WIDTH * PLATFORM_FB_HEIGHT)
#define PLATFORM_PALETTE_SIZE 256

/*
 * Present one complete frame.
 *
 * pixels:  PLATFORM_FB_SIZE bytes, row-major, top-left origin, no row
 *          padding. Each byte is an index into `palette`.
 * palette: PLATFORM_PALETTE_SIZE entries. Each entry is the *value*
 *          0x00RRGGBB (so it is endian-independent). The top byte is ignored.
 *
 * Both pointers must be non-NULL. The backend must have finished reading both
 * buffers before it returns: the caller is free to overwrite them
 * immediately. The palette applies to this frame only; nothing is latched
 * between calls.
 *
 * The call may block to pace output (vsync, a display transfer, or a host
 * frame limiter) but for no longer than roughly one tick period. Callers must
 * not use it as a clock: timing comes from platform_time_us() only.
 *
 * There is no failure return. A backend that cannot display drops the frame.
 */
void platform_present(const uint8_t *pixels, const uint32_t *palette);

/* ---- Input ------------------------------------------------------------- */

/*
 * Buttons are a bitmask of *currently held* controls. There are no events
 * and no edges: the engine derives presses and releases by comparing the
 * masks of consecutive ticks, which is what makes input recordable and
 * replayable.
 */
enum {
    PLATFORM_BTN_UP     = 1u << 0,
    PLATFORM_BTN_DOWN   = 1u << 1,
    PLATFORM_BTN_LEFT   = 1u << 2,
    PLATFORM_BTN_RIGHT  = 1u << 3,
    PLATFORM_BTN_A      = 1u << 4,
    PLATFORM_BTN_B      = 1u << 5,
    PLATFORM_BTN_X      = 1u << 6,
    PLATFORM_BTN_Y      = 1u << 7,
    PLATFORM_BTN_START  = 1u << 8,
    PLATFORM_BTN_SELECT = 1u << 9,
    /* Bits 10..30 are reserved and read as zero. */

    /*
     * The platform asks the application to stop: window closed, scripted
     * input exhausted, frame limit reached. Once set it stays set on every
     * later poll. Bare-metal backends never set it.
     */
    PLATFORM_BTN_QUIT   = 1u << 31
};

typedef struct platform_input {
    uint32_t buttons; /* OR of PLATFORM_BTN_* */
} platform_input_t;

/*
 * Sample the input state into *out (must be non-NULL). Every field of *out
 * is written on every call.
 *
 * The engine calls this exactly once per simulation tick, immediately before
 * that tick runs, and nowhere else. Backends rely on that: a scripted
 * backend hands out one script entry per call, so tick k always sees script
 * entry k regardless of wall-clock timing.
 *
 * Never blocks.
 */
void platform_poll_input(platform_input_t *out);

/* ---- Time -------------------------------------------------------------- */

/*
 * Monotonic time in microseconds since an arbitrary, fixed epoch.
 *
 * Guarantees: never decreases; does not jump with wall-clock adjustments;
 * does not wrap within any realistic run (64 bits of microseconds). The unit
 * is always microseconds but the resolution may be coarser.
 *
 * Only the fixed-timestep loop may turn this into ticks. Simulation code
 * must never read it: the simulation sees tick numbers only, otherwise runs
 * are not reproducible.
 *
 * Never blocks. Safe to call from either task.
 */
uint64_t platform_time_us(void);

/* ---- Audio ------------------------------------------------------------- */

/* The one audio format: signed 16-bit, mono, native endian. */
#define PLATFORM_AUDIO_RATE_HZ 22050

/*
 * Audio fill callback. Must write exactly `frame_count` samples to `frames`
 * (one sample per frame, since output is mono).
 *
 * It runs in a context the engine does not control: another thread on a
 * host, an interrupt on a microcontroller. It must therefore be short and
 * bounded, must not block, must not call any platform_* function, and may
 * only touch state that the engine has made safe to share with it.
 * frame_count varies from call to call and is never zero.
 */
typedef void (*platform_audio_fn)(void *user, int16_t *frames,
                                  uint32_t frame_count);

/*
 * Register `fn` as the audio source, replacing any previous one. `user` is
 * passed back verbatim. Passing fn == NULL unregisters and the output goes
 * silent.
 *
 * When this function returns, the previous callback is guaranteed not to be
 * running and will not be called again, so the caller may then tear down
 * whatever state it used.
 *
 * Returns true if the callback is installed and will be called. Returns
 * false if the platform has no usable audio output; the engine carries on
 * without sound. Unregistering always returns true.
 */
bool platform_audio_set_callback(platform_audio_fn fn, void *user);

/* ---- Storage ----------------------------------------------------------- */

/*
 * Persistent storage is a small array of slots. A slot holds one opaque
 * blob, replaced as a whole. There are no names, offsets or partial writes.
 */
#define PLATFORM_STORAGE_SLOTS     8u
#define PLATFORM_STORAGE_SLOT_MAX  (64u * 1024u) /* bytes per slot */

#define PLATFORM_STORAGE_EMPTY  (-1) /* slot has never been written */
#define PLATFORM_STORAGE_ERROR  (-2) /* bad argument or device failure */

/*
 * Read the whole blob in `slot` into `buf`, which has room for `capacity`
 * bytes.
 *
 * Returns the blob size in bytes (>= 0) on success. Returns
 * PLATFORM_STORAGE_EMPTY if the slot has never been written, and
 * PLATFORM_STORAGE_ERROR if slot >= PLATFORM_STORAGE_SLOTS, if the blob is
 * larger than `capacity`, or if the device fails. On any negative return
 * the contents of `buf` are unspecified. `buf` may be NULL only when
 * capacity is 0.
 *
 * May block for the duration of the device access.
 */
int32_t platform_storage_read(uint32_t slot, void *buf, uint32_t capacity);

/*
 * Replace the blob in `slot` with `size` bytes from `data`.
 *
 * Returns true on success. Returns false, leaving the previous contents
 * intact, if slot >= PLATFORM_STORAGE_SLOTS, size > PLATFORM_STORAGE_SLOT_MAX
 * or the device fails. A zero-size write is valid and stores an empty blob,
 * which then reads back as 0, not PLATFORM_STORAGE_EMPTY.
 *
 * Backends should make the replacement atomic with respect to power loss
 * (afterwards the slot holds either the old or the new blob) and must say so
 * in their own documentation if they cannot.
 *
 * May block for the duration of the device access.
 */
bool platform_storage_write(uint32_t slot, const void *data, uint32_t size);

#endif /* KERNEL_PLATFORM_H */
