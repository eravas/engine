/*
 * loop.h - fixed-timestep loop joining the simulation and render tasks.
 * CONTRACT ONLY.
 *
 * The implementation (kernel/loop.c) is intentionally absent. The contract
 * below is specified by tests/todo/test_loop.c.
 *
 * Two layers:
 *   loop_clock_t  pure arithmetic: microsecond timestamps in, whole ticks
 *                 out. No platform calls, so it is testable in isolation.
 *   loop_t        drives the two tasks in turn on one core, using
 *                 platform_time_us() and platform_poll_input().
 */
#ifndef KERNEL_LOOP_H
#define KERNEL_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/handoff.h"
#include "kernel/platform.h"

#define LOOP_TICK_HZ 30u

/*
 * Most ticks one loop_clock_advance() call may report. Backlog beyond this
 * is dropped, so a stall slows the game down instead of making it run an
 * unbounded burst of ticks to catch up.
 */
#define LOOP_MAX_CATCHUP_TICKS 4u

/* ---- Clock ------------------------------------------------------------- */

/*
 * The fields are the implementation's business and may be reshaped freely.
 * Note that one tick is 1000000/30 microseconds, which is not an integer:
 * an implementation that adds a rounded tick length drifts, and the tests
 * check that this one does not.
 */
typedef struct loop_clock {
    uint64_t last_us;  /* timestamp of the previous advance */
    uint32_t residue;  /* time not yet turned into ticks */
    uint64_t dropped;  /* ticks discarded by the catch-up limit */
    bool     primed;   /* false until the first advance */
} loop_clock_t;

/* Reset to "no time seen yet", with zero dropped ticks. */
void loop_clock_init(loop_clock_t *c);

/*
 * Feed the current time; get the number of ticks that are now due.
 *
 * The first call after loop_clock_init() only records `now_us` as the epoch
 * t0 and returns 0.
 *
 * Exactness: as long as the catch-up limit is never hit, the sum of all
 * return values after a call with time t is exactly
 *     floor((t - t0) * LOOP_TICK_HZ / 1000000)
 * regardless of how many calls were made or how they were spaced. In
 * particular there is no cumulative rounding drift.
 *
 * Catch-up limit: if more than LOOP_MAX_CATCHUP_TICKS whole ticks are due,
 * the call returns LOOP_MAX_CATCHUP_TICKS, adds the excess whole ticks to
 * the dropped count, and keeps the sub-tick remainder, so the phase of
 * later ticks is unchanged.
 *
 * A `now_us` earlier than the previous one is treated as no time having
 * passed (returns 0) and does not move the clock backwards.
 *
 * Must be correct for a single gap of at least one year of microseconds.
 */
uint32_t loop_clock_advance(loop_clock_t *c, uint64_t now_us);

/* Total ticks discarded by the catch-up limit since loop_clock_init(). */
uint64_t loop_clock_dropped(const loop_clock_t *c);

/* ---- Tasks ------------------------------------------------------------- */

typedef struct loop_tasks {
    void *ctx; /* passed to both callbacks verbatim */

    /*
     * Simulation task: advance the world by exactly one tick.
     *
     * tick:  0 for the first tick, then 1, 2, ... with no gaps, ever (dropped
     *        clock ticks do not consume tick numbers).
     * input: the input sampled for this tick. Valid during the call only.
     * out:   the hand-off to fill. out->tick is already set to `tick`; data
     *        and data_size hold whatever the previous tick left there.
     *
     * Return false to ask the loop to stop after this tick.
     * Must not call platform_time_us() or platform_present().
     */
    bool (*sim_tick)(void *ctx, uint64_t tick, const platform_input_t *input,
                     handoff_t *out);

    /*
     * Render task: turn a hand-off into a frame and present it.
     *
     * in: the hand-off as left by the most recent tick. Read-only, valid
     *     during the call only.
     *
     * Calls platform_present() itself. Must not touch simulation state
     * other than through `in`.
     */
    void (*render)(void *ctx, const handoff_t *in);
} loop_tasks_t;

/* The fields are the implementation's business and may be reshaped freely. */
typedef struct loop {
    loop_tasks_t tasks;
    loop_clock_t clock;
    handoff_t    handoff;
    uint64_t     next_tick;
    bool         stopped;
} loop_t;

/*
 * Prepare a loop. Copies *tasks; both callbacks must be non-NULL. The
 * hand-off starts as {tick 0, data NULL, data_size 0}. Calls no platform
 * function and neither callback.
 */
void loop_init(loop_t *l, const loop_tasks_t *tasks);

/*
 * Run one iteration, with both tasks taking turns on the calling context:
 *
 *   1. Read platform_time_us() exactly once and advance the clock.
 *   2. For each tick that is due, in order: call platform_poll_input()
 *      exactly once, then sim_tick() with that input. If sim_tick() returns
 *      false, skip the remaining due ticks.
 *   3. If at least one tick ran in step 2, call render() exactly once with
 *      the hand-off left by the last of them.
 *
 * If no tick is due, neither platform_poll_input() nor either callback is
 * called. Input is therefore consumed at exactly one poll per executed
 * tick, which is what makes a run a function of its input script alone.
 *
 * Returns true while the loop should keep going. Returns false once a
 * sim_tick() has returned false; that iteration still performs step 3. Any
 * later call does nothing at all (no platform calls, no callbacks) and
 * returns false.
 */
bool loop_step(loop_t *l);

/* Call loop_step() until it returns false. */
void loop_run(loop_t *l);

/* Number of ticks executed so far (equals the next tick index). */
uint64_t loop_tick_count(const loop_t *l);

#endif /* KERNEL_LOOP_H */
