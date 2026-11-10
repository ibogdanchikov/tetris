# tetris

A small falling-block puzzle game in C17 with SDL3, OpenGL 3.3 core and GLAD.

**Milestone 1** is the skeleton only: a window, a GL context, an instanced quad
renderer, a playfield view and a fixed-timestep loop. There are no falling
pieces, no input handling and no gravity yet — the board is filled with a
static demo pattern so the renderer can be checked.

## Requirements

- CMake >= 3.24, Ninja, a C17 compiler (gcc or clang)
- SDL3 (>= 3.2) and its development headers
- An OpenGL 3.3 core capable driver (Mesa llvmpipe works too)

On Ubuntu 24.04/26.04 and derivatives:

```sh
sudo apt-get install -y cmake ninja-build pkg-config libsdl3-dev libgl-dev
```

`libgl-dev` is only needed for the driver's development headers; the game itself
never links libGL — GLAD resolves the entry points through
`SDL_GL_GetProcAddress`. GLAD is vendored in `external/glad`, so no Python or
network access is needed to build.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
```

Other presets: `release`, and `debug-sanitizers` (AddressSanitizer +
UndefinedBehaviorSanitizer).

The build produces `build/<preset>/tetris`. Assets are copied next to the
executable as a post-build step, and shaders are loaded from
`<SDL_GetBasePath()>/assets/shaders`, so the binary works from any working
directory.

Useful cache options:

| Option | Default | Meaning |
| --- | --- | --- |
| `TETRIS_WARNINGS_AS_ERRORS` | `OFF` | Turn the project's warning set into errors |
| `TETRIS_ENABLE_SANITIZERS` | `OFF` | Build with ASan + UBSan |

## Run

```sh
./build/debug/tetris
```

Press Escape or close the window to quit.

### Command line flags

| Flag | Meaning |
| --- | --- |
| `--frames N` | Render N frames, then exit (smoke tests, screenshots) |
| `--screenshot PATH` | Write the last rendered frame to PATH as a BMP |
| `--size WxH` | Initial window size in pixels (default `800x600`) |
| `--seed N` | Simulation seed (default `0x853c49e6748fea9b`) |
| `--help` | Print usage |

Example: render two seconds worth of frames and grab the result.

```sh
./build/debug/tetris --frames 120 --screenshot /tmp/shot.bmp
```

Without `--frames` the screenshot is taken when the window closes, so it works
for interactive runs too.

## Tests

```sh
ctest --preset debug
ctest --preset release
ctest --preset debug-sanitizers
```

The tests cover the pure core (board, game state, RNG) and the playfield layout
maths. They are a plain assert-style harness — no external framework — and they
never link SDL or OpenGL.

## Project layout

```
CMakeLists.txt              build definition, targets, warnings, asset copy
CMakePresets.json           debug / release / debug-sanitizers presets
assets/shaders/             quad.vert, quad.frag (GLSL 330 core)
external/glad/              vendored GLAD2 loader (see its README)
src/core/                   pure game logic: no SDL, no GL, no malloc, no clock
  board.h/.c                playfield storage, row 0 is the bottom row
  game.h/.c                 GameState, game_init, game_step
  rng.h/.c                  splitmix64, state is a single uint64_t
src/platform/               SDL3 window and GL context lifecycle
  platform.h/.c
src/render/                 GL rendering
  shader.h/.c               file loading, compile/link with full info logs
  renderer.h/.c             instanced quad batcher (pixel rects, bottom-left origin)
  board_layout.h/.c         pure layout maths, GL-free and unit tested
  board_view.h/.c           playfield frame, grid and cell palette
src/app/                    the one place GL and SDL meet outside platform
  screenshot.h/.c           glReadPixels -> flipped rows -> SDL BMP
src/main.c                  CLI parsing and the fixed-timestep loop
tests/test_main.c           assert-style tests, run through ctest
```

Targets: `glad` (static), `tetris_core` (static), `tetris_view_layout` (static),
`tetris` (executable), `tetris_tests` (test executable).

## Conventions

- Fixed simulation rate of 60 Hz; rendering runs as fast as the display allows.
  Frame time is clamped to 0.25 s so a stall cannot cause a catch-up spiral, and
  the leftover accumulator is passed to the renderer as `alpha` for future
  interpolation.
- Pixel coordinates in the renderer have their origin in the **bottom-left**
  corner, which matches board row 0 being the bottom row.
- `#pragma once` everywhere, `snake_case`, module-prefixed function names.

## Notes

- The `debug-sanitizers` preset is separate from `debug` on purpose: under a
  Wayland session, SDL's libdecor/GTK window decoration libraries leak memory at
  exit, so LeakSanitizer makes the *game* exit non-zero with reports that point
  at GTK/pango/fontconfig, not at this project. The tests themselves are clean
  under ASan and UBSan.
- GLAD can be regenerated with the commands in `external/glad/README.md`.
