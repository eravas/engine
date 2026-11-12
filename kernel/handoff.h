/*
 * handoff.h - the only thing that passes from the simulation task to the
 * render task.
 *
 * The simulation task fills one handoff_t per tick; the render task turns
 * the most recent one into a frame. Nothing else is shared: the render task
 * never reads simulation state, and the simulation task never reads render
 * state.
 *
 * Roles
 *   - The simulation task is the only writer, and only inside its tick.
 *   - The render task is the only reader, and only inside its render call.
 *   - `data` points into memory owned by the simulation task. The render
 *     task keeps no pointer into a hand-off after its render call returns.
 *
 * Payload lifetime
 *   Not every hand-off is rendered: after a stall the loop runs several
 *   ticks and renders only the last. A payload is therefore in exactly one
 *   of these states, and the state says who may touch its memory:
 *
 *   published   The tick that wrote it has ended and nothing has replaced
 *               it. It is the one the next render call will take. The
 *               simulation task must leave it valid and unmodified.
 *   acquired    A render call has started with it. It stays valid and
 *               unmodified until that render call returns.
 *   released    The render call that acquired it has returned.
 *   superseded  A later tick published a newer payload before any render
 *               call acquired this one. It will never be rendered.
 *
 *   The simulation task may reclaim a payload's memory as soon as it is
 *   released or superseded, and not before. A payload never goes from
 *   acquired to superseded: being replaced as "the latest" while a render
 *   call holds it does not shorten its life.
 *
 * What this means for the per-frame arena
 *   One core, tasks in turn: a render call never overlaps a tick. When a
 *   tick begins, the previous payload is either released or still published;
 *   if published, no render call can take it before this tick publishes its
 *   own, so it is superseded in effect from the moment the tick starts. The
 *   simulation task may therefore rewind its per-frame arena at the start of
 *   every tick and build the new payload in the same memory.
 *
 *   Two cores: a tick can begin while a render call still holds an acquired
 *   payload, so the new payload must be built somewhere else. That takes at
 *   least two payload buffers, or three if the simulation task must never
 *   wait for the render task. The rules above already say which buffer is
 *   free; what is not designed yet is the primitive that publishes and
 *   acquires atomically, which is why it is absent from platform.h (see
 *   docs/architecture.md, Assumptions).
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
