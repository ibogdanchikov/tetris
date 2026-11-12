# tetris

A small falling-block puzzle game in C17 with SDL3, OpenGL 3.3 core and GLAD.

See `ARCHITECTURE.md` for modules and design decisions, and `ROADMAP.md` for completed and planned milestones.

## Download and play (Linux)

Download the game as a single file, make it executable and run it:

```sh
curl -LO https://github.com/ibogdanchikov/tetris/releases/latest/download/tetris-linux-x86_64
chmod +x tetris-linux-x86_64
./tetris-linux-x86_64
```

Nothing else needs to be installed. The file is a 64-bit x86 executable with
SDL3 and the shaders built in. It needs glibc 2.34 or newer (Ubuntu 22.04,
Debian 12, Fedora 35 or later), an X11 or Wayland desktop and an OpenGL 3.3
driver. The controls are listed under [Controls](#controls).

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

**Milestone 4, part 1** adds a line-clear delay to the core: completed rows are
scored at the lock but stay on the board for 24 ticks before they are removed
and the next piece spawns, while input keeps charging DAS for the next piece.

**Milestone 4, part 2** upgrades the presentation: tetrominoes get a beveled
block style, the ghost becomes a crisp outline, the renderer can switch between
alpha and additive blending, and a shader-drawn animated background sits behind
the board. F5 recompiles the shaders in place and `--assets DIR` points the game
at a working copy, so shader iteration does not need a rebuild.

**Milestone 4, part 3** adds the effects layer: the full rows now flash white and
shrink from the centre columns outwards while they wait out the clear delay,
filled cells burst into glow particles in their own colours, a lock flashes the
piece and kicks up a little dust, a hard drop leaves a vertical trail and shakes
the board, and a level-up pulses the playfield frame. The effects live in a
GL-free, allocation-free `src/fx` module driven only by the game's event bits, so
the simulation and its seed are untouched.

**Milestone 4, part 4** adds sound. Every effect is synthesised from short note
lists at startup — no audio files and no dependency beyond SDL audio — then
pre-rendered into mono float buffers and mixed by a fixed voice pool on SDL's
audio callback. Moves, rotations, hold, hard drops, locks, line clears (one
arpeggio per count, a brighter chord for a tetris), level up, game over and pause
each have a short sound, and a soft clip keeps stacked sounds in range. `M`
mutes, `--mute` starts muted, and a missing audio device only logs a warning:
the game runs silent rather than failing.

**Milestone 5** adds screens and a save file. The game opens on a title screen,
and a main menu starts a run, picks the start level and leads to the high scores
and the settings. Play pauses into a menu, and a finished run shows a game-over
menu with its place in the table. The settings cover the volume, the effects
intensity, DAS, ARR and the key bindings. They and the top ten scores are saved
to a small text file in the user's data directory.

**Milestone 6** adds the modern scoring extras: T-spins by the 3-corner rule,
mini T-spins, back-to-back bonuses, combos and perfect clears, scored from a
table. A label in the left panel names a notable clear (for example **B2B**,
**T-SPIN**, **DOUBLE**, **COMBO 2**) and fades out after a moment.

**Milestone 7, part 1** lets the rows above a line clear fall into place. Once
the cleared rows are gone, the rows that moved are drawn where they were and drop
to their new place over 0.15 seconds, speeding up as they go. Only the drawing
changes: the rows are removed on the same tick as before, and the score and the
replay timing are untouched.

**Milestone 7, part 2** adds bloom. The scene is drawn into an offscreen
texture, its brightest parts are taken down to a quarter of the size and
blurred, and the blur is added back over the scene, so the neon tubes, blocks
and text glow into the dark around them. **Settings > BLOOM** turns it off and
on; it is on by default.

**Milestone 7, part 3** adds two particle types. A hard drop throws hot sparks
sideways from the bottom cells of the landed piece, harder the farther it fell.
A big clear (four lines, a T-spin that clears lines, or a perfect clear) sends a
neon shockwave ring out from the cleared rows; a perfect clear sends a second,
wider one. Both follow the **EFFECTS** setting.

**Milestone 7, part 4** adds music: an original 16-second loop synthesized at
startup like the effects, with no audio files. It plays on a voice of its own,
outside the effect pool, so effects never cut it off. **Settings > MUSIC** sets
its level on top of **VOLUME**, and **M** mutes it with everything else.

**Milestone 7, part 5** replaces the blocky text with a vector font: every
letter is a few neon tubes on the same 5x7 grid as before, drawn from a
distance-field texture atlas that is built at startup. Text stays smooth at any
size, the glow comes from the same texture, and the menus now fit their text to
the room they have instead of stepping a whole pixel at a time. The old pixel
font stays as the fallback.

## Requirements

- CMake >= 3.24, Ninja, a C17 compiler (gcc or clang)
- SDL3 (>= 3.2) and its development headers
- An OpenGL 3.3 core capable driver (Mesa llvmpipe works too; turn **BLOOM** off
  there for a faster frame, see [Bloom](#bloom))

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
directory. `--assets DIR` loads them from `DIR/shaders` instead, which is what
makes live shader editing convenient. The shaders are also compiled into the
executable (`cmake/embed_shaders.cmake`), and those built-in copies are used when
there is no `assets/shaders` directory next to it.

Useful cache options:

| Option | Default | Meaning |
| --- | --- | --- |
| `TETRIS_WARNINGS_AS_ERRORS` | `OFF` | Turn the project's warning set into errors |
| `TETRIS_ENABLE_SANITIZERS` | `OFF` | Build with ASan + UBSan |

### Linux release binary

```sh
packaging/linux/build.sh
```

This builds `dist/tetris-linux-x86_64`, the file behind the download link above.
It needs only Docker: the build runs in an Ubuntu 22.04 image
(`packaging/linux/Dockerfile`) with SDL3 compiled as a static library, so the
binary links nothing but glibc. SDL still opens X11, Wayland, OpenGL, PipeWire,
PulseAudio and ALSA at run time with `dlopen`, so the player's own drivers are
used. The script also runs the tests and prints the binary's shared libraries
and the newest glibc version it needs.

`.github/workflows/release.yml` runs the same script. Pushing a `v*` tag publishes
the binary on a GitHub release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

## Run

```sh
./build/debug/tetris
```

The game opens on the title screen; Enter opens the main menu. Escape goes back
one screen and quits from the title, and closing the window always quits.
`--play` skips the title and starts a run right away.

F5 recompiles every shader from the active asset directory. A shader that fails
to compile is logged and the previous program stays in use, so a typo never
takes the window down. The background shader is optional: if it fails to load
the game keeps the flat clear colour instead. To edit the repository shaders
without rebuilding, run from the repo root against the source assets:

```sh
./build/debug/tetris --assets assets
```

Then edit `assets/shaders/*.frag` and press F5.

### Controls

The game keys, with their defaults. Each action can have two keys, and
**Settings > Controls** rebinds them:

| Key | Action |
| --- | --- |
| Left / Right arrows | Move left / right (DAS then ARR while held) |
| Down arrow | Soft drop (+1 point per row) |
| Space | Hard drop (+2 points per row) |
| Up / X | Rotate clockwise |
| Z / Left Ctrl | Rotate counter-clockwise |
| C / Left Shift | Hold |

These keys are fixed and cannot be bound to an action:

| Key | Action |
| --- | --- |
| Escape | Pause while playing; back one screen in a menu; quit from the title |
| P | Pause or resume |
| R | Restart with a fresh seed (while playing, paused or after game over) |
| M | Mute or unmute the sound |
| F5 | Reload all shaders (for iteration) |

In the menus the arrows move and change values, Enter selects, Escape or
Backspace goes back, and Delete empties a key slot on the controls screen. Space
does not select, so a player still pressing hard drop when the stack tops out
does not skip the game-over screen.

Keys are mapped by scancode, so they follow key positions and work on any
keyboard layout. Holding a key is the game's business: the OS key repeat is
ignored and DAS/ARR produce the repeats. In the menus a held arrow repeats.

### Screens

- **Title**: the seven pieces, the best score so far and **PRESS ENTER**.
- **Main menu**: **START**, **LEVEL** (the start level, 1 to 15, changed with
  Left and Right), **HIGH SCORES**, **SETTINGS** and **QUIT**.
- **Pause**: **RESUME**, **RESTART**, **SETTINGS** and **MAIN MENU**, over the
  dimmed board. Leaving for the main menu drops the run without a score.
- **Game over**: the score, **NEW HIGH SCORE!** or the run's place in the table,
  **PLAY AGAIN** and **MAIN MENU**.
- **High scores**: the top ten runs with their score, level and lines. The run
  that just ended is highlighted.
- **Settings**: **VOLUME** (0 to 10), **MUSIC** (0 to 10, on top of the volume;
  0 turns it off), **EFFECTS** (0 to 10: particles, shake,
  flashes, trails and the level-up pulse; 0 turns them off), **BLOOM** (on or
  off), **DAS** and **ARR** (shown in milliseconds; ARR 0 sends the piece
  straight to the wall) and **CONTROLS**. Changes take effect at once, also in a
  paused run.
- **Controls**: two key slots per action. Enter waits for a key, Escape cancels,
  and a key that another action already uses moves to the new one.
  **RESET KEYS** puts the defaults back.

### Save file

Settings and the top ten scores are saved to `save.txt` in SDL's preference
directory, `~/.local/share/tetris/save.txt` on Linux (`$XDG_DATA_HOME` is
honored). It is plain text: a `TETRIS SAVE` line, `version=1`, then one
`key=value` per line, with keys as SDL scancode numbers and each score as
`score level lines seed`:

```
TETRIS SAVE
version=1
volume=7
music=7
effects=10
bloom=1
das=10
arr=2
start_level=1
key.left=80 0
key.hold=6 225
score=12400 4 31 9600400441103183515
```

The game writes it through a temporary file that is renamed over the old one, so
a crash in the middle of a save keeps the previous file. Settings are written when
you leave the settings screens, and a score as soon as the run ends. A missing
file gives the defaults; a file without the magic line or with an unknown version
logs a warning and gives the defaults too. Unknown keys and malformed lines are
skipped, and values out of range are clamped.

### HUD

The playfield sits between two four-cell panels, with one cell of empty space on
each side:

- **HOLD** on the left, a greyed piece when a hold has already been spent this
  drop. Below it are **SCORE**, **LEVEL** and **LINES**, labels above
  left-aligned values.
- **NEXT** on the right, the next three pieces stacked top to bottom.
- A **clear label** in pink under **LINES** after a notable clear: a T-spin, a
  four-line clear, a back-to-back bonus, a combo or a perfect clear. It lists
  what applies, one line each: **B2B**, **MINI**, **T-SPIN**, the clear
  (**SINGLE**, **DOUBLE**, **TRIPLE** or **QUAD**), **COMBO n**, and **PERFECT
  CLEAR**. It stays up 1.6 seconds and fades over the last 0.4. Its text shrinks
  until every line fits the panel, so it never covers the board.

Preview pieces use three quarters of the board cell size, so a flat I keeps about
half a cell of padding inside its four-cell box instead of touching the frame.

Text is drawn with a stroke font, `src/render/stroke_font.c`: each glyph is a
few polylines on a 5x7 grid, drawn as round-capped tubes like a neon sign. At
startup the strokes are turned into a signed-distance atlas, a 512x480
one-channel texture in which each texel holds how far it is from the nearest
stroke. One quad per glyph samples it with `RECT_STYLE_GLYPH`, which gives a
crisp antialiased edge at any size and, in an additive second pass, the glow
around the letters. There are no font files. The font covers space, A-Z, 0-9
and `: . - ! ? < >`; lowercase letters use the uppercase glyph, and any other
character is skipped.

The stroke font has the same metrics as the old 5x7 pixel font, which is still
there (`src/render/font.c`) as the fallback when the atlas cannot be made or a
custom `quad.frag` has no glyph style. Sizes are in units of one pixel-font
pixel and may be fractional; the pixel font rounds them down. A ten-digit score
shrinks until it fits the panel; from a 352x352 framebuffer up (cell size 16)
even `4294967295` fits.

Pause dims the board and puts the pause menu over it. Losing window focus pauses
a running game so the piece does not keep falling, and resuming clears the input
buffer and the accumulator, so a key held during the pause does not fire on the
first tick. **R** restarts and unpauses; a finished run shows the game-over menu
instead.

### Bloom

With **BLOOM** on, `renderer_begin` points drawing at an offscreen texture the
size of the window, rebuilt whenever the window size changes. `renderer_end`
then runs three full-screen passes: `bloom_extract.frag` keeps what is brighter
than 0.75 (the largest color channel, with a soft knee) at a quarter of the
size, `bloom_blur.frag` blurs it twice along x and then y, and
`bloom_composite.frag` draws the scene with the blur added at 1.2 times its
strength. The result ends up in the window's back buffer, so screenshots include
it. If the bloom shaders or buffers cannot be built, the game logs it and draws
without bloom.

Frame times measured with Mesa llvmpipe (software rendering, the static stack
from the screenshots, vsync on but never reached):

| Size | Bloom off | Bloom on |
| --- | --- | --- |
| 800x600 | 18.6 ms | 25.5 - 27.0 ms |
| 1920x1080 | 39 ms | 57 ms |

On the Intel Iris Xe GPU both stay at the 16.7 ms vsync interval at 1920x1080.

### Audio

There are no audio files. `src/audio/synth.c` renders every effect from a short
note list when the game starts: a note is a sine, triangle, soft square or noise
burst with a linear pitch sweep, a linear attack/release envelope and an optional
one-pole low-pass. Every note starts and ends at zero and the effects peak below
0.6, so nothing clicks and several sounds can overlap without harshness.

The sounds are a quiet tick per move, a short upward slide for rotate and hold, a
low thud for a hard drop, a soft thump for a lock, a rising arpeggio for one to
three cleared lines, a brighter chord sweep for a tetris, a short fanfare for a
level up, a descending phrase for game over, and a two-tone blip for pause and
resume. Soft drop is deliberately silent: it fires many times a second and has no
sound of its own.

The music is one loop of 8 bars at 120 beats per minute in A minor (Am F C G Am F
Dm E): an octave bass in eighths, a soft sine pad, a quiet square arpeggio in
sixteenths, a kick, a brushed snare and closed hats, and a lead melody written
for this game. `synth_music_render` builds it from a chord per bar and a melody
table, and writes it as a loop: the part of a note that runs past the end is
added at the start, where it would sound on the next pass, so the end of the
buffer runs straight into its start. It peaks at 0.51 and is about 3 MB at
48 kHz.

`src/audio/audio.c` is the only code that talks to SDL audio. It opens the
default playback device with a mono float32 stream at 48 kHz — SDL converts to
whatever the device wants — and mixes up to 16 voices from a callback on SDL's
audio thread. Voices are stolen oldest-first when the pool is full, the sum is
soft-clipped so a stack cannot pass full scale, and the mute change is faded over
about 60 ms rather than stepping the volume. The music plays from a looping voice
next to that pool, under the same master fade (so **VOLUME** and mute apply) times
its own level from **MUSIC**, which fades the same way; it starts playing at
launch, fades in, and keeps going through menus, pauses and restarts. The app thread takes
`SDL_LockAudioStream` around the voice list and the mute flag; the callback
already runs with that lock held, so one lock covers both threads.

Audio is optional. If the subsystem, the device or the buffers cannot be set up,
the game logs one warning, every `audio_*` call becomes a no-op and everything
else works as before. `M` mutes and unmutes, `--mute` starts muted, and opening a
device is logged with its driver, name, format and rate.

### Rules

- **Rotation** uses SRS. A rotation that does not fit in place tries the five
  standard wall kicks for that rotation, and is rejected only if none of them
  fits. O has no kicks. The index of the kick that was used is kept in
  `last_kick_index` for T-spin detection.
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
- **T-spins** use the 3-corner rule. A T locks as a T-spin when the last thing
  that moved it was a rotation and at least three of the four cells diagonal to
  its center are filled or outside the board. It is a full T-spin when both
  corners on the side the T points to are filled, or when the rotation needed
  the fifth (last) wall kick; otherwise it is a mini. A shift, a row of gravity
  or soft drop, or a hard drop that falls at least one row ends the spin; a hard
  drop in place keeps it.
- **Scoring** is a table, multiplied by the level in force *before* the clear:

  | Lines | Plain | Mini T-spin | T-spin | Perfect clear bonus |
  | --- | --- | --- | --- | --- |
  | 0 | 0 | 100 | 400 | - |
  | 1 | 100 | 200 | 800 | 800 |
  | 2 | 300 | 400 | 1200 | 1200 |
  | 3 | 500 | - | 1600 | 1800 |
  | 4 | 800 | - | - | 2000 (3200 back-to-back) |

  A four-line clear or a T-spin that clears lines is *difficult*. A difficult
  clear right after another one gets its line points times 1.5 (back-to-back).
  Any other line clear breaks that chain; a lock that clears nothing does not.
  Every clearing lock in a row after the first adds a combo of 50 per step, and
  a lock that clears nothing ends the combo. A perfect clear (the board is
  empty once the rows are gone) adds its bonus on top. Soft drop adds 1 per row,
  hard drop 2 per row.
- **Line clear delay**: the score, line count and event happen at the lock, but
  the full rows stay on the board for `GAME_LINE_CLEAR_DELAY_TICKS` (24) ticks.
  Input is ignored during the delay, except that a held LEFT/RIGHT keeps
  charging DAS, so the next piece inherits that progress instead of restarting
  it. When the timer runs out the rows are removed,
  `GAME_EVENT_CLEAR_DONE` fires and the next piece spawns. A lock that clears nothing spawns immediately, and a lock out still
  ends the game on the lock tick.
- **Level** is `start_level + lines_cleared / 10`, so the run levels up every ten
  lines. The start level is picked on the main menu and saved with the
  settings.
- **DAS and ARR** default to 10 and 2 ticks and come from the settings. They are
  part of the run's `GameState`, so the core stays free of files and settings.
- **Game over** is a block out (the new piece cannot spawn) or a lock out (a
  piece locks with every cell in the hidden buffer rows). A game that is over
  only advances the tick counter.

### Command line flags

| Flag | Meaning |
| --- | --- |
| `--frames N` | Render N frames, then exit (smoke tests, screenshots) |
| `--screenshot PATH` | Write the last rendered frame to PATH as a BMP |
| `--assets DIR` | Load shaders from `DIR/shaders` instead of next to the binary (or the built-in copies) |
| `--size WxH` | Initial window size in pixels (default `800x600`) |
| `--seed N` | Simulation seed (default `0x853c49e6748fea9b`) |
| `--mute` | Start with the sound muted (`M` toggles it) |
| `--play` | Skip the title and the menu and start a run right away |
| `--help` | Print usage |

Example: render two seconds worth of frames of a run and grab the result.
Without `--play` the screenshot shows the title screen.

```sh
./build/debug/tetris --play --frames 120 --screenshot /tmp/shot.bmp
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
line-clear delay and its DAS charging, the lock delay stall bounds, the event
bits, the recorded cleared rows and the lock / hard-drop effect data. The effects
suite checks particle lifetimes and the fixed pool bound, deterministic spawning
from the fx seed, that the game's RNG is untouched, the event-to-effect mapping
(colours, rows, trails, shake), the dt clamp, the shake decay and the
clear-animation keyframes. For the sparks and rings it checks the spawn counts
and placement, that nothing spawns with the effects off, the lifetimes, the
spark gravity, ring growth, and that neither pool grows past its capacity. The audio suite checks every effect's length, peak,
finite samples, click-free ends and determinism, the short-capacity path, the
mixer (silence, gains, voices ending mid-block, the soft clip, the master ramp)
and the event-to-sound precedence, and that the music loop has the right length,
stays under its peak, renders the same twice, steps from its last sample back to
its first no more than around the seam, and plays beside a full effect pool
without taking a slot; the looping mixer is checked for wrapping, its gain ramp
and keeping its place while muted. The save suite runs the save file parser and
serializer on strings: a round trip, missing, corrupt and unknown-version files,
clamped and malformed values, key bindings and the score table. The scoring
suite checks the T-spin rule corner by corner, a real T-spin double from a
rotation, minis and the fifth-kick upgrade, losing a spin by dropping,
back-to-back, combos, perfect clears, the clear label text and its fade, and
that a run with spins replays exactly. They are a plain
assert-style harness — no external framework — and they never link SDL or
OpenGL.

## Project layout

```
CMakeLists.txt              build definition, targets, warnings, asset copy, shader embedding
CMakePresets.json           debug / release / debug-sanitizers presets
cmake/embed_shaders.cmake   turns assets/shaders into a generated C file for the built-in copies
packaging/linux/            Dockerfile and build.sh for the single-file Linux binary
.github/workflows/release.yml  builds that binary and attaches it to a release on a v* tag
assets/shaders/             quad.vert, quad.frag, background.vert, background.frag and the three
                            bloom passes: bloom_extract, bloom_blur, bloom_composite (GLSL 330 core)
external/glad/              vendored GLAD2 loader (see its README)
src/core/                   pure game logic: no SDL, no GL, no malloc, no clock
  board.h/.c                playfield storage, row 0 is the bottom row, hidden buffer rows on top
  piece.h/.c                SRS tetromino shapes, four rotations each, y-up offsets
  bag.h/.c                  7-bag randomiser over the game's rng state
  input.h                   held/pressed actions for one tick
  game.h/.c                 GameState, game_init, game_step (kicks, hold, gravity, lock delay, T-spins, scoring)
  rng.h/.c                  splitmix64, state is a single uint64_t
src/fx/                     presentation effects: no SDL, no GL, no malloc, no clock
  fx.h/.c                   particle pool (glow dots, sparks), shockwave rings, effect timers,
                            screen shake, clear-animation maths, row drop, clear label
src/audio/                  sound: synthesis and playback
  synth.h/.c                note tables, waveforms, envelopes, sound lengths (SDL-free, tested)
  mix.h/.c                  voice mixing, master ramp and the soft clip (SDL-free, tested)
  audio.h/.c                the SDL audio device, stream callback and voice pool
src/platform/               SDL3 window and GL context lifecycle
  platform.h/.c
src/render/                 GL rendering
  shader.h/.c               file loading, compile/link with full info logs
  builtin_shaders.h         lookup of the shaders compiled into the binary (the .c is generated)
  renderer.h/.c             instanced quad batcher with styles, blend modes, background and F5 reload
  board_layout.h/.c         pure layout maths for the board and both HUD panels, GL-free and tested
  font.h/.c                 built-in 5x7 bitmap font data and measuring, GL-free and tested
  stroke_font.h/.c          the vector stroke font and its distance atlas, GL-free
  text.h/.c                 draws strings from the atlas, or as pixel-font rectangles
  board_view.h/.c           playfield, ghost, drop beam, active piece and its halo, and the clear animation
  fx_view.h/.c              lock flash, hard-drop trails and additive glow particles
  hud.h/.c                  hold, next, stats and the clear label
  menu_view.h/.c            title, menus, settings, controls and high-score screens
src/app/                    the one place GL and SDL meet outside platform
  screenshot.h/.c           glReadPixels -> flipped rows -> SDL BMP
  input_buffer.h/.c         SDL-free press/hold buffer between frames and ticks
  input_sdl.h/.c            scancode mapping and SDL event handling
  sound_map.h/.c            GAME_EVENT_* bits to sound requests, precedence only (SDL-free, tested)
  save.h/.c                 settings, key bindings, the score table and the save text format (SDL-free, tested)
  save_file.h/.c            finding, reading and atomically replacing the save file
  menu.h/.c                 the screen state machine and its menus (SDL-free)
src/main.c                  CLI parsing, the screens and the fixed-timestep loop
tests/test_main.c           assert-style harness, board / RNG / layout suites
tests/test_piece.c          piece shape and 7-bag suites
tests/test_game.c           gameplay suites: spawn, movement, gravity, lock delay, clears
tests/test_rules.c          milestone 3 suites: kicks, hold, queue, levels, scoring, events
tests/test_font.c           glyph coverage, lowercase mapping and width arithmetic
tests/test_input_buffer.c   input buffer suite plus a tap-through-game_step check
tests/test_fx.c             particle, event-mapping, shake and clear-animation suites
tests/test_audio.c          synthesis, mixing and event-to-sound precedence suites
tests/test_save.c           save file parser, serializer, bindings and score table suites
tests/test_scoring.c        T-spin rule, back-to-back, combo, perfect clear and clear label suites
```

Targets: `glad` (static), `tetris_core` (static), `tetris_fx` (static),
`tetris_view_layout` (static), `tetris_input` (static), `tetris_audio` (static),
`tetris_app` (static), `tetris` (executable), `tetris_tests` (test executable).

## Conventions

- Fixed simulation rate of 60 Hz; rendering runs as fast as the display allows.
  Frame time is clamped to 0.25 s so a stall cannot cause a catch-up spiral, and
  the leftover accumulator is passed to the views as `alpha`. The line-clear
  animation is the one thing that interpolates with it.
- The board stores 24 rows: 20 visible ones plus four hidden buffer rows where
  pieces spawn and rotate out of sight. Row 0 is the bottom row.
- Gameplay durations are tick counts (`GAME_LOCK_DELAY_TICKS`, `GAME_DAS_TICKS`,
  ...), never wall-clock time, so a recorded input sequence replays exactly.
  Gravity is a per-tick increment in 1/65536 of a row (`gravity_accum`), which
  keeps it integer and lets high levels fall several rows in one tick.
- Renderers and other consumers of `GAME_EVENT_*` must remember that events are
  per tick: a frame that runs several ticks has to OR them together.
- Effects are presentation only and live in `src/fx`, which knows nothing about
  SDL or GL. It holds a fixed 1024-particle pool, a few effect timers and its own
  splitmix64 state; the game's `rng_state` is never touched, so effects cannot
  change the piece order. When the pool is full new spawns are dropped rather
  than overwriting older particles. Positions are board cells, so the view scales
  them with the layout and nothing depends on the window size.
- `main.c` calls `fx_on_events` once per frame with the OR-ed event bits and then
  `fx_update` with the clamped frame time. Pause freezes both, and a restart
  resets the effects. A frame that has no events still updates the timers.
- Sound is presentation only, like the effects: the core never knows about it.
  `sound_map_events` turns the frame's OR-ed bits into `SoundRequest`s (a clear
  replaces the lock click, a hard drop replaces it too, game over suppresses both
  and plays its phrase) and `audio_play` starts them. The mix is a sum of
  voices times their gains and the master gain, then a soft clip.
- The synth and the mixer are SDL-free, deterministic and tested; `audio.c` is
  the only SDL audio code. Every effect is pre-rendered once at init, so the
  callback never synthesises or allocates. Voices and the mute flag are guarded
  by `SDL_LockAudioStream`, which the callback already holds while it runs; a
  failed `audio_init` is not fatal and turns every call into a no-op.
- The move tick is throttled in `main.c` to one sound per six ticks: ARR fires
  every two ticks, and a tick at 30 Hz would buzz. A soft drop is silent for the
  same reason.
- The line-clear animation is derived from `GameState`, not from the fx timers:
  `t = (game_clear_progress + alpha) / GAME_LINE_CLEAR_DELAY_TICKS` drives
  `fx_clear_visual`, which is a pure, tested function. `board_view` only scales
  each cell around its centre and mixes the colour towards white.
- The screen shake is a decaying triangle wave in board cells; the app applies it
  by translating the whole layout, so the playfield and the HUD move together and
  no drawing code needs to know about the offset.
- Particles and trails are drawn with `RECT_STYLE_GLOW` and additive blending and
  clipped to half a cell around the playfield, so a burst never paints over the
  HUD. Particles have a kind: glow dots for line clears and lock dust, and sparks
  for hard drops, which fall under stronger gravity and are drawn as short
  streaks pushed towards white. Shockwave rings have a pool of their own
  (`FX_MAX_RINGS`, 4) and are drawn with `RECT_STYLE_NEON` as a tube along a
  circle that grows fast and then slows. The lock flash is the only alpha-blended effect; the level-up pulse
  brightens the additive neon frame.
- After a clear, the rows that moved fall into place over `FX_ROW_DROP_SECONDS`
  (0.15 s). `fx` keeps the removed rows and works out how far each row fell;
  `board_view` draws the locked cells that many cells higher, easing to 0, and
  clips them at the top of the playfield. A lock during the fall ends it.
- Pixel coordinates in the renderer have their origin in the **bottom-left**
  corner, which matches board row 0 being the bottom row.
- The layout centres the left panel, playfield and right panel as one group, so
  both panels keep a one-cell gap at every window size. Panels are four cells
  wide and top-aligned with the playfield.
- Text is the one textured thing: one quad per glyph from the stroke font's
  distance atlas (`RECT_STYLE_GLYPH`), plus a glow pass from the same atlas. The
  atlas is built from in-repo stroke data at startup, so there are no font files.
  Values longer than the stats panel shrink their glyph size instead of running
  over the board.
- Every quad instance is three `vec4`s: rect, colour and
  `(style, p0, p1, p2)`. Styles are chosen in `quad.frag`, so a beveled cell
  costs exactly the same one instance as a flat rectangle. `renderer_push_rect`
  is the flat convenience; `renderer_push_styled` selects any other style.
  The look is neon arcade, and every light comes from the shapes themselves:
  locked cells, the active piece and the HUD previews are neon glass blocks
  with a soft glow around them, the ghost is a hologram silhouette
  (`RECT_STYLE_GHOST`) with a beam of light (`RECT_STYLE_BEAM`) falling to it
  from the piece, the piece has a stronger halo, and the board and the HUD boxes
  have neon tube frames (`RECT_STYLE_NEON`). Text glows; grid cells and the
  overlay dimming stay flat.
- Blending is GL state: `renderer_set_blend` flushes the batch before it
  changes, and `renderer_begin` resets it to alpha every frame.
- The animated background is one full-screen triangle generated from
  `gl_VertexID` with an empty VAO. Its time comes from the app, is presentation
  only and wraps at an hour. The block sheen in `quad.frag` uses the same time. A missing or broken background shader is not
  fatal: the flat clear colour shows through and F5 can bring it back.
- Shader reloading is intentionally forgiving: `renderer_reload_shaders` only
  swaps a program in after it compiled and linked, so F5 over a typo keeps the
  previous program running and just logs the error.
- Pause lives in `main.c`, not in `GameState`: the simulation does not know about
  it. A pause clears the input buffer and the accumulator, so no key pressed
  while paused fires on resume.
- Keyboard state is buffered between frames: a press and its release can both
  land between two ticks, and it must still reach exactly one tick. Main calls
  `input_buffer_take` once per `game_step`.
- `#pragma once` everywhere, `snake_case`, module-prefixed function names.

## Future work

- An audio-device picker, and more than one music track.
- Saving a run or recording a replay.
- Lowercase letters and more punctuation in the stroke font, if the game ever
  needs them.

## Notes

- The `debug-sanitizers` preset is separate from `debug` on purpose: under a
  Wayland session, SDL's libdecor/GTK window decoration libraries leak memory at
  exit, so LeakSanitizer makes the *game* exit non-zero with reports that point
  at GTK/pango/fontconfig, not at this project. The tests themselves are clean
  under ASan and UBSan.
- GLAD can be regenerated with the commands in `external/glad/README.md`.
