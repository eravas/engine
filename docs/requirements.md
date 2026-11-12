# Requirements

The fixed decisions for the engine scaffold. Each has an ID so that
[validation.md](validation.md) can say how it is checked.

## Language and code rules

| ID | Requirement |
| --- | --- |
| R1 | C11, compiled with `-Wall -Wextra -Werror`. No C++. Our own code is also held to strict ISO C11 with `-Wpedantic`. |
| R2 | Engine code uses integer and fixed-point arithmetic only. No floats. |
| R3 | Behaviour is deterministic across targets. |
| R4 | No heap after start-up. All engine memory is one static block of 480 KiB, split into arenas and pools. No `malloc`/`free` outside platform backends. |
| R5 | Only platform backends may touch SDL, the OS or chip SDKs. |
| R6 | Third-party code is SDL3, in the SDL backend only. Nothing else. |

## Target

| ID | Requirement |
| --- | --- |
| R7 | The floor target is the RP2350: dual Cortex-M33, 520 KiB RAM. The engine block plus a 32 KiB platform reserve must fit its 512 KiB main RAM region. The host build obeys the same limits. |
| R8 | Display is 320×240, 8-bit indexed. |
| R9 | The simulation tick is fixed at 30 Hz. |
| R10 | Simulation and rendering are two tasks joined by a hand-off struct. They must also run correctly in turn on one core. |

## Platform contract

| ID | Requirement |
| --- | --- |
| R11 | `kernel/platform.h` is the full contract: present frame with palette, poll input, monotonic time in microseconds, audio callback registration, storage read and write. Every function is documented. |
| R12 | An SDL backend and a headless backend implement the contract. |
| R13 | The headless build needs no SDL, takes scripted input, and can dump frames as PPM files. |
| R14 | An RP2350 backend exists as a compile-only stub on the Pico SDK. |

## Build and CI

| ID | Requirement |
| --- | --- |
| R15 | CMake presets `host-sdl`, `host-headless` and `rp2350`. |
| R16 | `host-sdl` uses a system SDL3 if present, otherwise fetches it from GitHub. |
| R17 | GitHub Actions builds all presets and runs the tests on headless. |

## Tests

| ID | Requirement |
| --- | --- |
| R18 | A minimal, dependency-free test runner. |
| R19 | The arena allocator, the pool allocator with generation handles, and the fixed-timestep loop exist as headers with documented contracts, plus tests that specify them. Those tests carry a separate `todo` label that CI reports but does not fail on. |
| R20 | A determinism harness runs N ticks from scripted input, hashes the state, and compares two runs. |

## Change from the original brief

The brief fixed the block at 512 KB. That cannot link for the RP2350: its
520 KiB of RAM is a 512 KiB main region plus two 4 KiB banks that hold the
stacks, and the SDK's own data shares the main region. The block is now
480 KiB. `kernel/memory.h` has the arithmetic.

## Out of scope for the scaffold

Not implemented, by decision: arena, pool and handle logic; the body of the
fixed-timestep loop; any blitter, draw list, tilemap, field or simulation
code.
