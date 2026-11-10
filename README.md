# tetris

A small falling-block puzzle game in C17 with SDL3, OpenGL 3.3 core and GLAD.

**Milestone 1** is the skeleton only: a window, a GL context, an instanced quad
renderer, a playfield view and a fixed-timestep loop.

**Milestone 2, part 1** adds the pure game logic under `src/core`: the 7-bag
randomiser, SRS piece shapes, gravity, lock delay, DAS/ARR movement and line
clears, all counted in 60 Hz ticks.

**Milestone 2, part 2** connects it to the keyboard and draws the whole game:
the active piece, its ghost, the next-piece preview, the top-out overlay and the
line count in the window title. There is no text rendering yet, no scoring and
no animations.

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

### Controls

| Key | Action |
| --- | --- |
| Left / Right arrows | Move left / right (DAS then ARR while held) |
| Down arrow | Soft drop |
| Space | Hard drop |
| Up / X | Rotate clockwise |
| Z / Left Ctrl | Rotate counter-clockwise |
| R | Restart with a fresh seed |
| Escape | Quit |

Keys are mapped by scancode, so they follow key positions and work on any
keyboard layout. Holding a key is the game's business: the OS key repeat is
ignored and DAS/ARR produce the repeats.

The window title shows the line count (`Tetris — lines N`) and switches to
`Tetris — game over (R to restart)` when a piece cannot spawn. It is the only
text in the game until a font renderer arrives.

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

The tests cover the pure core (board, pieces, 7-bag, game state, RNG), the
playfield layout maths and the input buffer, including a small tick-loop check
that a tap between two ticks moves a piece exactly one column. They are a plain
assert-style harness — no external framework — and they never link SDL or
OpenGL.

## Project layout

```
CMakeLists.txt              build definition, targets, warnings, asset copy
CMakePresets.json           debug / release / debug-sanitizers presets
assets/shaders/             quad.vert, quad.frag (GLSL 330 core)
external/glad/              vendored GLAD2 loader (see its README)
src/core/                   pure game logic: no SDL, no GL, no malloc, no clock
  board.h/.c                playfield storage, row 0 is the bottom row, hidden buffer rows on top
  piece.h/.c                SRS tetromino shapes, four rotations each, y-up offsets
  bag.h/.c                  7-bag randomiser over the game's rng state
  input.h                   held/pressed actions for one tick
  game.h/.c                 GameState, game_init, game_step (gravity, lock delay, clears)
  rng.h/.c                  splitmix64, state is a single uint64_t
src/platform/               SDL3 window and GL context lifecycle
  platform.h/.c
src/render/                 GL rendering
  shader.h/.c               file loading, compile/link with full info logs
  renderer.h/.c             instanced quad batcher (pixel rects, bottom-left origin, scissor clip)
  board_layout.h/.c         pure layout maths, GL-free and unit tested
  board_view.h/.c           playfield, ghost, active piece and next-piece panel
src/app/                    the one place GL and SDL meet outside platform
  screenshot.h/.c           glReadPixels -> flipped rows -> SDL BMP
  input_buffer.h/.c         SDL-free press/hold buffer between frames and ticks
  input_sdl.h/.c            scancode mapping and SDL event handling
src/main.c                  CLI parsing and the fixed-timestep loop
tests/test_main.c           assert-style harness, board / RNG / layout suites
tests/test_piece.c          piece shape and 7-bag suites
tests/test_game.c           gameplay suites: spawn, movement, gravity, lock delay, clears
tests/test_input_buffer.c   input buffer suite plus a tap-through-game_step check
```

Targets: `glad` (static), `tetris_core` (static), `tetris_view_layout` (static),
`tetris_input` (static), `tetris` (executable), `tetris_tests` (test executable).

## Conventions

- Fixed simulation rate of 60 Hz; rendering runs as fast as the display allows.
  Frame time is clamped to 0.25 s so a stall cannot cause a catch-up spiral, and
  the leftover accumulator is passed to the renderer as `alpha` for future
  interpolation.
- The board stores 24 rows: 20 visible ones plus four hidden buffer rows where
  pieces spawn and rotate out of sight. Row 0 is the bottom row.
- Gameplay durations are tick counts (`GAME_GRAVITY_TICKS`, `GAME_LOCK_DELAY_TICKS`,
  ...), never wall-clock time, so a recorded input sequence replays exactly.
- Pixel coordinates in the renderer have their origin in the **bottom-left**
  corner, which matches board row 0 being the bottom row.
- The layout centres the playfield, gap and next-piece panel as one group, so the
  panel keeps a one-cell gap at every window size.
- Keyboard state is buffered between frames: a press and its release can both
  land between two ticks, and it must still reach exactly one tick. Main calls
  `input_buffer_take` once per `game_step`.
- `#pragma once` everywhere, `snake_case`, module-prefixed function names.

## Notes

- The `debug-sanitizers` preset is separate from `debug` on purpose: under a
  Wayland session, SDL's libdecor/GTK window decoration libraries leak memory at
  exit, so LeakSanitizer makes the *game* exit non-zero with reports that point
  at GTK/pango/fontconfig, not at this project. The tests themselves are clean
  under ASan and UBSan.
- GLAD can be regenerated with the commands in `external/glad/README.md`.
