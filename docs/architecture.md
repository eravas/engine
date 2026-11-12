# Architecture

## Layers

```
game/   sim/          portable engine code (placeholders today)
   \     /
   kernel/            platform.h, memory, loop; later renderer and fields
      |
  platform.h          the only interface downwards
      |
platform/sdl   platform/headless   platform/rp2350
```

Dependencies point downwards only. Code in `kernel/`, `sim/` and `game/` is
portable C11: integers only, no heap, and no includes other than its own
headers and a few freestanding C headers. Only a backend may include SDL, OS
or chip-SDK headers. A backend never includes anything from above except
`kernel/platform.h`.

## Directory layout

| Path | Contents |
| --- | --- |
| `kernel/` | `platform.h` (contract), `memory.h` (budget), and contract-only headers `arena.h`, `pool.h`, `loop.h`, `handoff.h` |
| `platform/sdl/` | Desktop window backend on SDL3 |
| `platform/headless/` | Deterministic backend for tests: virtual clock, scripted input, PPM dumps |
| `platform/rp2350/` | Pico SDK backend, compile-only stub |
| `sim/`, `game/` | Placeholders |
| `tools/` | Placeholder for the asset cooker |
| `tests/` | Test runner, backend tests, determinism harness, lint, smoke app, `todo/` specifications |
| `docs/` | These documents |

## Platform contract

`kernel/platform.h` declares six capabilities and one convention:

- `platform_present`: one 320×240 indexed frame plus its 256-entry palette.
- `platform_poll_input`: the held-button mask, sampled once per tick.
- `platform_time_us`: monotonic microseconds.
- `platform_audio_set_callback`: register the function that fills audio.
- `platform_storage_read` / `platform_storage_write`: persistent slots.
- `app_main`: defined by the application; the backend owns the real entry
  point and calls it.

## Memory

One static block of 480 KiB (`MEMORY_BLOCK_SIZE`) is the engine's entire
memory. It is split into arenas (bump allocation, rewound or reset as a
whole) and pools (fixed-size items addressed by generation handles). There is
no heap after start-up. The host uses the same block size as the RP2350.

The RP2350's 520 KiB is not one piece. With the Pico SDK's default memory
map, static data lives in a 512 KiB main region and each core's stack in a
4 KiB scratch bank. The block shares the main region with the platform:

| Part of main RAM | Size | Basis |
| --- | --- | --- |
| Engine block | 480 KiB | `MEMORY_BLOCK_SIZE` |
| SDK with the stub backend, including its 2 KiB minimum heap | 3.2 KiB | measured |
| A real backend: display, audio, flash and stdio buffers | 12 KiB | estimate |
| Moving both stacks into main RAM at 8 KiB each, if 4 KiB proves too small | 16 KiB | estimate |

The last three rows are the 32 KiB platform reserve. `tests/budget` links a
real 480 KiB block for the RP2350 on every build, and the `ram_budget` test
reports how much of the reserve is in use.

## Tasks and the loop

Simulation and rendering are separate tasks. The simulation task advances
the world by one tick at a fixed 30 Hz and fills a hand-off struct; the
render task reads that struct and presents a frame. They share nothing else.
The loop runs them in turn on one core: due ticks first, then one render.
Because the hand-off is the only link and has a single writer and a single
reader, the same two tasks can later run on the RP2350's two cores.

The hand-off's payload belongs to the simulation task, which may reuse its
memory once the payload is released (its render call has returned) or
superseded (a newer one was published before any render took it). On one
core that allows the per-frame arena to be rewound at the start of every
tick. On two cores it requires two payload buffers, or three if the
simulation must never wait. `kernel/handoff.h` has the full rules.

## Determinism

Engine code uses integers and fixed point only. The simulation never reads
the clock; it sees tick numbers and one input sample per tick. A run is
therefore a function of its input script, which the headless backend
supplies and the determinism harness checks by hashing state.

## Build

CMake with three presets, each selecting one backend through
`ENGINE_PLATFORM`:

| Preset | Backend | Output |
| --- | --- | --- |
| `host-headless` | headless | tests, `smoke`, `budget` |
| `host-sdl` | SDL3 (system, else fetched) | `smoke` in a window, storage test |
| `rp2350` | Pico SDK stub | `smoke.elf`, `budget.elf`, RAM budget check |

```
cmake --preset host-headless
cmake --build --preset host-headless
ctest --preset host-headless      # must pass
ctest --preset todo               # specifications of unwritten code
```

## Assumptions

Choices made where the brief was silent. Each is the simplest option that
satisfied the fixed decisions; all are open to change.

**Platform contract**

1. The backend owns `main()` and calls `app_main()`. Needed because SDL and
   the Pico SDK each have their own start-up path.
2. A palette entry is the value `0x00RRGGBB`. The palette is passed with
   every frame and nothing is latched.
3. `platform_present` may block to pace output. The SDL backend limits
   itself to 30 Hz with vsync off, so tearing is possible.
4. Input is a mask of held buttons (d-pad, A, B, X, Y, Start, Select) plus a
   Quit bit. There are no events. The engine polls exactly once per tick.
   The remaining bits are reserved and always zero.
5. Audio is signed 16-bit mono at 22050 Hz, pulled through a fill callback
   that may run on another thread or in an interrupt.
6. Storage is 8 slots of at most 64 KiB, each read and written as a whole
   blob. The SDL backend keeps one file per slot under `./storage`, and
   treats a slot as empty only when its file does not exist.
7. The contract has no sleep and no second-core primitive. The ownership
   rules for the hand-off payload are settled; the primitive that publishes
   and acquires it atomically across cores is not designed yet. Until then
   "in turn on one core" is the reference behaviour.

**Headless backend**

8. The virtual clock advances by a fixed step on every query (default
   1000 µs), so time depends only on the number of queries.
9. An input script is a text file of `<buttons> [<ticks>]` lines. When it
   runs out, every poll returns Quit. With no script, `--frames N` supplies N
   idle ticks. A script may only describe input a real backend could
   produce: reserved bits are rejected at load rather than masked, so that a
   typo fails loudly, and Quit stays set once a script entry raises it.
10. Storage is held in memory and lost at exit, which keeps runs hermetic.

**Contracts for the unwritten components**

11. Arena allocations are zero-filled and a zero-size request returns NULL.
12. A handle is an opaque 32-bit value with 0 as null. A pool holds at most
    65535 items. A handle may be mistaken for live again after its slot has
    been reused 65535 times.
13. The loop runs at most 4 catch-up ticks per iteration and drops the rest.
    It renders only after at least one tick has run, and the render task
    calls `platform_present` itself.
14. The hand-off struct holds the tick number and an opaque payload pointer
    until a draw list exists.
15. No stub `.c` files are provided for these components. Their tests fail
    to link until the real file exists.

**Build and tests**

16. `kernel/CMakeLists.txt` globs `kernel/*.c`, so a new implementation file
    is picked up on the next build without a CMake edit.
17. `-Wall -Wextra -Wpedantic -Werror` and strict C11 apply to this
    repository's code only, not to SDL or Pico SDK sources. `-Wpedantic` goes
    beyond the brief; it keeps `platform.h` free of compiler extensions.
18. The `rp2350` configuration enables C++ and assembly because the Pico SDK
    compiles its own sources in those languages. No engine code uses them.
19. Pinned versions: SDL `release-3.4.20`, Pico SDK `2.3.1`, board `pico2`.
    The Pico SDK comes from `PICO_SDK_PATH` if set, otherwise it is fetched.
    It is a build-time dependency of the RP2350 backend only.
20. Tests run on the host only. The `smoke` and `budget` apps are platform
    test fixtures, not engine code. `smoke` exists so that something links
    against every backend; `budget` stands in for the real engine block
    until the allocator work defines it.
21. Engine code may include only `<stdalign.h>`, `<stdbool.h>`, `<stddef.h>`,
    `<stdint.h>`, `<stdnoreturn.h>`, `<limits.h>` and `<string.h>`. A small
    source linter in `tests/lint` enforces this together with the no-float
    and no-malloc rules.
22. Host presets build Debug; `rp2350` builds Release.
23. The memory budget assumes the Pico SDK's default memory map and its
    default 2 KiB minimum heap. The 32 KiB platform reserve is mostly
    estimate (see Memory) and should be revisited when a real backend exists.
    Stack depth is unmeasured: each core has at most 4 KiB while its stack
    stays in a scratch bank.
