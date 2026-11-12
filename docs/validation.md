# Validation

How each requirement in [requirements.md](requirements.md) is checked, and
where it is not.

## Running

```
cmake --preset host-headless && cmake --build --preset host-headless
ctest --preset host-headless    # labels unit, smoke, determinism, lint
ctest --preset todo             # label todo: expected to fail for now

cmake --preset host-sdl && cmake --build --preset host-sdl
ctest --preset host-sdl         # smoke and storage tests on dummy drivers

cmake --preset rp2350 && cmake --build --preset rp2350
ctest --preset rp2350           # RAM budget of the ELF; runs on the host
```

Build before running `ctest`: the list of files given to the linter is
refreshed by the build step.

## Test labels

| Label | What it covers | CI |
| --- | --- | --- |
| `unit` | Headless backend against `platform.h` and its own documented behaviour; SDL storage against `platform.h` | must pass |
| `budget` | The engine block plus platform data fit the RP2350's main RAM | must pass |
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
| R1 | Compiler flags on every target of ours, including `-Wpedantic`; CI builds with gcc and clang | |
| R2 | `lint`: rejects `float`, `double` and floating literals in `kernel/`, `sim/`, `game/` | Lexical only. A float hidden behind a macro or typedef from an allowed header would pass. |
| R3 | `determinism`: two runs hash equal, and the hash equals a recorded constant under gcc and clang | Only a toy subject exists. The harness has not run on the RP2350. |
| R4 | `lint`: rejects `malloc`, `calloc`, `realloc`, `free` and `<stdlib.h>` in engine code | Nothing measures usage inside the block until arenas exist. |
| R5 | `lint`: engine code may include only its own headers and a fixed list of C headers | |
| R6 | Review of the build files: SDL is referenced in `platform/sdl` only | The Pico SDK is a build dependency of the RP2350 backend. |
| R7 | CI job `rp2350` cross-compiles and links for Cortex-M33. `budget`: a real 480 KiB block links, and platform use stays inside the 32 KiB reserve | Not run on hardware. Measured against the stub backend only; a real backend's RAM and both stack depths are estimates. |
| R8 | `unit`: frame size and palette through present and PPM dump | |
| R9 | `todo`: loop clock tests (exact 30 Hz, no drift) | Unmet until the loop is written. |
| R10 | `todo`: loop tests (order of ticks and render, hand-off contents) | In-turn execution only. The payload lifetime rules in `handoff.h` are prose: nothing tests them, and the two-core primitive is not designed. |
| R11 | Review of `kernel/platform.h`; `unit` exercises every function | |
| R12 | `unit` for headless; `smoke` and storage `unit` tests on SDL's dummy drivers for SDL | SDL input, audio output and real window presentation have no automated test. |
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
`det_run` hands back the hash so that a test can also pin it to a constant,
which is what turns "same on this machine" into "same on every target".

A script the backend rejects (too many runs, reserved button bits) is a
setup failure, reported separately from "same" and "different", because a
run that never received its input proves nothing.

To cover a real system later, describe it as a `det_subject_t` (state block,
init, tick) and add it to `tests/test_determinism.c`.
