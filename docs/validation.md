# Validation

How each requirement in [requirements.md](requirements.md) is checked, and
where it is not.

## Running

```
cmake --preset host-headless && cmake --build --preset host-headless
ctest --preset host-headless    # labels unit, smoke, determinism, lint
ctest --preset todo             # label todo: expected to fail for now
```

Build before running `ctest`: the list of files given to the linter is
refreshed by the build step.

## Test labels

| Label | What it covers | CI |
| --- | --- | --- |
| `unit` | Headless backend against `platform.h` and its own documented behaviour | must pass |
| `smoke` | The smoke app run through a backend's real `main()` | must pass |
| `determinism` | The determinism harness and its self-test | must pass |
| `lint` | Engine-code rules, plus the linter's own fixtures | must pass |
| `todo` | Specifications of arena, pool and loop | reported, never fails the job |

A `todo` component shows as "build failed, not run" until `kernel/<name>.c`
exists, then as pass or fail per test. When one passes, move it out of the
`todo` loop in `tests/CMakeLists.txt` so that it becomes mandatory.

## Requirement coverage

| ID | Checked by | Limits |
| --- | --- | --- |
| R1 | Compiler flags on every target of ours; CI builds with gcc and clang | |
| R2 | `lint`: rejects `float`, `double` and floating literals in `kernel/`, `sim/`, `game/` | Lexical only. A float hidden behind a macro or typedef from an allowed header would pass. |
| R3 | `determinism`: two runs hash equal, and the hash equals a recorded constant under gcc and clang | Only a toy subject exists. The harness has not run on the RP2350. |
| R4 | `lint`: rejects `malloc`, `calloc`, `realloc`, `free` and `<stdlib.h>` in engine code | The 512 KB limit is a constant only. Nothing measures usage until arenas exist. |
| R5 | `lint`: engine code may include only its own headers and a fixed list of C headers | |
| R6 | Review of the build files: SDL is referenced in `platform/sdl` only | The Pico SDK is a build dependency of the RP2350 backend. |
| R7 | CI job `rp2350` cross-compiles and links for Cortex-M33 | Not run on hardware. RAM use is not checked against 520 KB. |
| R8 | `unit`: frame size and palette through present and PPM dump | |
| R9 | `todo`: loop clock tests (exact 30 Hz, no drift) | Unmet until the loop is written. |
| R10 | `todo`: loop tests (order of ticks and render, hand-off contents) | In-turn execution only. Two-core execution is not designed. |
| R11 | Review of `kernel/platform.h`; `unit` exercises every function | |
| R12 | `unit` for headless; `smoke` on SDL's dummy drivers for SDL | SDL input, audio output and real window presentation have no automated test. |
| R13 | CI job `host-headless` installs no SDL; `unit` covers script parsing and PPM bytes | |
| R14 | CI job `rp2350` | Every device function is a stub. |
| R15 | CI configures all three presets | |
| R16 | CI job `host-sdl` takes the fetch path | The system-SDL path is not exercised in CI. |
| R17 | `.github/workflows/ci.yml` | |
| R18 | Used by every test program | The runner has no tests of its own. |
| R19 | `todo` label; CI step "Todo tests" | |
| R20 | `determinism` | |

## Determinism harness

`tests/support/determinism.h` runs a subject for N ticks, feeding it one
input sample per tick from a headless input script, then hashes the
subject's state block with FNV-1a. `det_check` does this twice and compares.
`det_run` returns the hash so that a test can also pin it to a constant,
which is what turns "same on this machine" into "same on every target".

To cover a real system later, describe it as a `det_subject_t` (state block,
init, tick) and add it to `tests/test_determinism.c`.
