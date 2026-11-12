/*
 * determinism.h - run N ticks from scripted input, hash the state, compare.
 *
 * The harness drives a "subject": a block of state plus a function that
 * advances it by one tick given that tick's input. It feeds the subject from
 * the headless backend's input script, exactly one platform_poll_input() per
 * tick, which is the same path the real loop uses. It never consults the
 * clock, so wall time cannot leak in through the harness itself.
 *
 * What a pass means: the subject's state after N ticks is a function of the
 * script alone *within this build*. Comparing the hash against a constant
 * recorded from another target (det_run + CHECK_EQ_U) extends that claim
 * across compilers and CPUs.
 *
 * Requirements on the subject:
 *   - All mutable state it uses is inside the `state` block. Hidden statics
 *     defeat the comparison.
 *   - The block is hashed byte for byte. It is zero-filled before init(), so
 *     struct padding hashes consistently provided the subject never copies
 *     whole structs over it from uninitialised storage.
 *   - Pointers stored in the block hash by address. Store handles or
 *     offsets, as the engine does anyway.
 *
 * Requires the headless backend. det_run() calls headless_reset().
 */
#ifndef TESTS_SUPPORT_DETERMINISM_H
#define TESTS_SUPPORT_DETERMINISM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/platform.h"
#include "platform/headless/headless.h"

typedef struct det_subject {
    void  *state;
    size_t state_size;

    /* Bring the zero-filled state to its tick-0 value. May be NULL. */
    void (*init)(void *state);

    /* Advance by one tick. `tick` counts from 0. */
    void (*tick)(void *state, uint64_t tick, const platform_input_t *input);
} det_subject_t;

typedef struct det_script {
    const headless_input_run_t *runs;
    uint32_t                    run_count;
} det_script_t;

/* FNV-1a, 64-bit. Chain calls by passing the previous result as `seed`. */
#define DET_HASH_SEED 0xcbf29ce484222325ull
uint64_t det_hash(const void *data, size_t size, uint64_t seed);

/*
 * Reset the headless backend, install `script`, zero and init the state,
 * run `ticks` ticks and return the hash of the state. Ticks beyond the end
 * of the script see PLATFORM_BTN_QUIT, as any headless client would.
 */
uint64_t det_run(const det_subject_t *subject, const det_script_t *script,
                 uint64_t ticks);

/*
 * Run twice and compare. Returns true if both runs hash the same; the hash
 * of the first run is stored in *hash_out when it is non-NULL.
 */
bool det_check(const det_subject_t *subject, const det_script_t *script,
               uint64_t ticks, uint64_t *hash_out);

#endif /* TESTS_SUPPORT_DETERMINISM_H */
