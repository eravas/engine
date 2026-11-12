/*
 * handoff.h - the only thing that passes from the simulation task to the
 * render task.
 *
 * The simulation task fills one handoff_t per tick; the render task turns
 * the most recent one into a frame. Nothing else is shared: the render task
 * never reads simulation state, and the simulation task never reads render
 * state.
 *
 * Ownership rules (they are what lets the two tasks move to separate cores
 * later without changing either):
 *   - The simulation task is the only writer, and only inside its tick.
 *   - The render task is the only reader, and only inside its render call.
 *   - `data` points into memory owned by the simulation task. It must stay
 *     valid and unmodified from the end of the tick that set it until the
 *     render call that consumes it has returned.
 *   - The render task keeps no pointer into a hand-off after it returns.
 *
 * Run in turn on one core, "tick, then render" satisfies these rules
 * trivially. On two cores they require two hand-offs and a swap; the
 * primitive for that swap is not part of platform.h yet (see
 * docs/architecture.md, Assumptions).
 */
#ifndef KERNEL_HANDOFF_H
#define KERNEL_HANDOFF_H

#include <stdint.h>

typedef struct handoff {
    /* Index of the tick whose end state this describes. Set by the loop. */
    uint64_t tick;

    /*
     * Payload produced by the simulation task. Opaque for now; it becomes
     * the per-frame draw list. NULL with size 0 means "nothing to draw".
     */
    const void *data;
    uint32_t    data_size;
} handoff_t;

#endif /* KERNEL_HANDOFF_H */
