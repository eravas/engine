/*
 * headless.h - control surface of the headless backend.
 *
 * The headless backend implements kernel/platform.h with no window, no
 * device and no wall clock, so a run is a pure function of its inputs. This
 * header is how a test (or the headless main) sets those inputs up and
 * inspects the outputs. Engine code must never include it.
 *
 * Behaviour of the platform.h functions in this backend:
 *
 *   platform_time_us     Virtual clock. Returns the current virtual time,
 *                        then advances it by the configured step. Time
 *                        starts at 0 and the default step is 1000 us per
 *                        query, so a loop that only polls the clock always
 *                        makes progress.
 *   platform_poll_input  Returns the next entry of the input script. Once
 *                        the script is exhausted (or if none was set) it
 *                        returns PLATFORM_BTN_QUIT alone, forever.
 *   platform_present     Copies the frame, counts it, and optionally writes
 *                        it as a binary PPM (P6). Never blocks.
 *   platform_audio_*     Stores the callback and returns true. Nothing
 *                        calls it except headless_audio_pump().
 *   platform_storage_*   Slots held in memory. Lost at process exit.
 */
#ifndef PLATFORM_HEADLESS_H
#define PLATFORM_HEADLESS_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/platform.h"

/* One run of an input script: `buttons` is returned by `count` polls. */
typedef struct headless_input_run {
    uint32_t buttons;
    uint32_t count;
} headless_input_run_t;

#define HEADLESS_MAX_INPUT_RUNS 4096u

/*
 * Return every piece of backend state to its start-up value: time 0, step
 * 1000 us, no script, no frames presented, dumping off, no audio callback,
 * all storage slots empty.
 */
void headless_reset(void);

/* ---- Time ---- */
void headless_set_time_us(uint64_t now_us);       /* must not go backwards */
void headless_set_time_step_us(uint64_t step_us); /* 0 freezes the clock */
void headless_advance_time_us(uint64_t delta_us);

/* ---- Input ---- */

/*
 * Replace the input script with a copy of `runs`. Returns false, leaving no
 * script set, if run_count > HEADLESS_MAX_INPUT_RUNS. Runs with count 0 are
 * skipped.
 */
bool headless_set_input_script(const headless_input_run_t *runs,
                               uint32_t run_count);

/*
 * Load an input script from a text file. One run per line:
 *
 *     <buttons> [<count>]
 *
 * <buttons> is a PLATFORM_BTN_* mask in C notation (0x11, 17, 021) and
 * <count> the number of ticks it is held (default 1). Blank lines and
 * anything after '#' are ignored. Returns false on I/O or syntax errors
 * (reported on stderr), leaving no script set.
 */
bool headless_load_input_script(const char *path);

/* Polls performed since the script was set, including polls past its end. */
uint64_t headless_poll_count(void);

/* ---- Display ---- */

/*
 * Write every `every`-th presented frame (the first, then each every-th
 * after it) to <dir>/frame_NNNNNN.ppm, where NNNNNN is the zero-based index
 * of the frame among all frames presented. The directory must exist.
 * dir == NULL or every == 0 turns dumping off. Returns false if `dir` is
 * too long to store.
 */
bool headless_set_frame_dump(const char *dir, uint32_t every);

/* Frames presented since the last reset. */
uint64_t headless_frame_count(void);

/*
 * The backend's copy of the most recently presented frame, or NULL for both
 * if none has been presented. Valid until the next present or reset.
 */
void headless_last_frame(const uint8_t **pixels, const uint32_t **palette);

/*
 * Write an indexed frame as a binary PPM. Returns false on I/O error. Also
 * used internally for frame dumps.
 */
bool headless_write_ppm(const char *path, const uint8_t *pixels,
                        const uint32_t *palette);

/* False if any frame dump has failed to write since the last reset. */
bool headless_frame_dump_ok(void);

/* ---- Audio ---- */

/*
 * Call the registered audio callback synchronously for `frame_count`
 * samples into `frames`. Returns false, leaving `frames` untouched, if no
 * callback is registered or frame_count is 0.
 */
bool headless_audio_pump(int16_t *frames, uint32_t frame_count);

#endif /* PLATFORM_HEADLESS_H */
