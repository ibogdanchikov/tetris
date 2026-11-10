# tetris

A small falling-block puzzle game in C17 with SDL3, OpenGL 3.3 core and GLAD.

**Milestone 1** is the skeleton only: a window, a GL context, an instanced quad
renderer, a playfield view and a fixed-timestep loop.

**Milestone 2, part 1** adds the pure game logic under `src/core`: the 7-bag
randomiser, SRS piece shapes, gravity, lock delay, DAS/ARR movement and line
clears, all counted in 60 Hz ticks.

**Milestone 2, part 2** connects it to the keyboard and draws the whole game:
the active piece, its ghost, the next-piece preview and the top-out overlay.

**Milestone 3, part 1** makes the rules complete in the core: SRS wall kicks,
hold, a three-piece next queue, guideline gravity per level, scoring and levels,
lock out detection and a per-tick event bitmask for presentation.

**Milestone 3, part 2** adds the on-screen HUD: a built-in 5x7 pixel font drawn
from rectangles, HOLD and NEXT panels, the score / level / lines readout, pause
and the paused and game-over overlays. The window title is plain `Tetris` again.

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
| Down arrow | Soft drop (+1 point per row) |
| Space | Hard drop (+2 points per row) |
| Up / X | Rotate clockwise |
| Z / Left Ctrl | Rotate counter-clockwise |
| C / Left Shift | Hold |
| P | Pause or resume |
| R | Restart with a fresh seed |
| Escape | Quit |

Keys are mapped by scancode, so they follow key positions and work on any
keyboard layout. Holding a key is the game's business: the OS key repeat is
ignored and DAS/ARR produce the repeats.

### HUD

The playfield sits between two four-cell panels, with one cell of empty space on
each side:

- **HOLD** on the left, a greyed piece when a hold has already been spent this
  drop. Below it are **SCORE**, **LEVEL** and **LINES**, labels above
  left-aligned values.
- **NEXT** on the right, the next three pieces stacked top to bottom.

Preview pieces use three quarters of the board cell size, so a flat I keeps about
half a cell of padding inside its four-cell box instead of touching the frame.

All text comes from a built-in 5x7 bitmap font drawn with the same rectangle
renderer as the board: no textures and no font files. The font covers space,
A-Z, 0-9 and `: . - ! ?`; lowercase letters use the uppercase glyph, and any
other character is skipped. A ten-digit score shrinks its glyph size a pixel at a
time until it fits the panel; from a 352x352 framebuffer up (cell size 16) even
`4294967295` fits.

Pause dims the board and puts **PAUSED** and **PRESS P** over it. Losing window
focus pauses a running game so the piece does not keep falling, and resuming
clears the input buffer and the accumulator, so a key held during the pause does
not fire on the first tick. **R** restarts and unpauses; a finished run shows
**GAME OVER** and **PRESS R** instead.

### Rules

- **Rotation** uses SRS. A rotation that does not fit in place tries the five
  standard wall kicks for that rotation, and is rejected only if none of them
  fits. O has no kicks. The index of the kick that was used is kept in
  `last_kick_index`; T-spin detection is future work.
- **Hold** swaps the active piece with the hold slot. The first hold takes the
  front of the next queue, later ones swap. The incoming piece spawns like a
  fresh piece, and hold is spent until the next piece locks.
- **Next queue** holds three pieces and is fed from the 7-bag, so every aligned
  group of seven pieces is still one permutation.
- **Gravity** depends on the level, in 1/65536 of a row per tick from a static
  table built from the guideline formula. Level 1 is one row per 60.015 ticks,
  so the first fall of a fresh piece happens on tick 61. Above level 20 the
  level 20 speed is used. Soft drop is the faster of 20x the level gravity and
  one row per two ticks.
- **Lock delay** is 30 ticks with at most 15 move/rotation resets per descent.
  Descending into a row the piece has not been in before refreshes both the
  timer and the reset budget; sliding or spinning off the ground without
  reaching a new row does not.
- **Scoring** is 100 / 300 / 500 / 800 for a single / double / triple / tetris,
  multiplied by the level in force *before* the clear. Soft drop adds 1 per row,
  hard drop 2 per row.
- **Level** is `start_level + lines_cleared / 10`, so the run levels up every ten
  lines. `--seed` is the only start-level control for now; the core also offers
  `game_init_at_level`.
- **Game over** is a block out (the new piece cannot spawn) or a lock out (a
  piece locks with every cell in the hidden buffer rows). A game that is over
  only advances the tick counter.
- T-spins, back-to-back bonuses, combos and perfect clears are not implemented
  yet.

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
playfield and HUD layout maths, the built-in font and the input buffer, including
a small tick-loop check that a tap between two ticks moves a piece exactly one
column. The rules suite covers every SRS kick case that could get the y direction
wrong, hold, the next queue, the gravity table, scoring and levels, lock out, the
lock delay stall bounds, the event bits and the recorded cleared rows. They are a
plain assert-style harness — no external framework — and they never link SDL or
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
  game.h/.c                 GameState, game_init, game_step (kicks, hold, gravity, lock delay, scoring)
  rng.h/.c                  splitmix64, state is a single uint64_t
src/platform/               SDL3 window and GL context lifecycle
  platform.h/.c
src/render/                 GL rendering
  shader.h/.c               file loading, compile/link with full info logs
  renderer.h/.c             instanced quad batcher (pixel rects, bottom-left origin, scissor clip)
  board_layout.h/.c         pure layout maths for the board and both HUD panels, GL-free and tested
  font.h/.c                 built-in 5x7 bitmap font data and measuring, GL-free and tested
  text.h/.c                 draws strings as merged pixel-run rectangles
  board_view.h/.c           playfield, ghost and active piece
  hud.h/.c                  hold, next, stats and the paused / game-over overlays
src/app/                    the one place GL and SDL meet outside platform
  screenshot.h/.c           glReadPixels -> flipped rows -> SDL BMP
  input_buffer.h/.c         SDL-free press/hold buffer between frames and ticks
  input_sdl.h/.c            scancode mapping and SDL event handling
src/main.c                  CLI parsing and the fixed-timestep loop
tests/test_main.c           assert-style harness, board / RNG / layout suites
tests/test_piece.c          piece shape and 7-bag suites
tests/test_game.c           gameplay suites: spawn, movement, gravity, lock delay, clears
tests/test_rules.c          milestone 3 suites: kicks, hold, queue, levels, scoring, events
tests/test_font.c           glyph coverage, lowercase mapping and width arithmetic
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
- Gameplay durations are tick counts (`GAME_LOCK_DELAY_TICKS`, `GAME_DAS_TICKS`,
  ...), never wall-clock time, so a recorded input sequence replays exactly.
  Gravity is a per-tick increment in 1/65536 of a row (`gravity_accum`), which
  keeps it integer and lets high levels fall several rows in one tick.
- Renderers and other consumers of `GAME_EVENT_*` must remember that events are
  per tick: a frame that runs several ticks has to OR them together.
- Pixel coordinates in the renderer have their origin in the **bottom-left**
  corner, which matches board row 0 being the bottom row.
- The layout centres the left panel, playfield and right panel as one group, so
  both panels keep a one-cell gap at every window size. Panels are four cells
  wide and top-aligned with the playfield.
- The HUD is drawn from rectangles too: one per run of lit font pixels. There are
  no textures and no font files. Values longer than the stats panel shrink their
  glyph size instead of running over the board.
- Pause lives in `main.c`, not in `GameState`: the simulation does not know about
  it. A pause clears the input buffer and the accumulator, so no key pressed
  while paused fires on resume.
- Keyboard state is buffered between frames: a press and its release can both
  land between two ticks, and it must still reach exactly one tick. Main calls
  `input_buffer_take` once per `game_step`.
- `#pragma once` everywhere, `snake_case`, module-prefixed function names.

## Future work

- T-spins, back-to-back bonuses, combos and perfect clears in the core.
- Menus, a start-level picker and a persistent high-score table.
- Audio for locks, line clears, hold and game over.
- Saving a run or recording a replay.
- Real font rendering (a texture atlas) once the HUD needs more characters or
  smoother type; the rectangle font is deliberate for now.

## Notes

- The `debug-sanitizers` preset is separate from `debug` on purpose: under a
  Wayland session, SDL's libdecor/GTK window decoration libraries leak memory at
  exit, so LeakSanitizer makes the *game* exit non-zero with reports that point
  at GTK/pango/fontconfig, not at this project. The tests themselves are clean
  under ASan and UBSan.
- GLAD can be regenerated with the commands in `external/glad/README.md`.
