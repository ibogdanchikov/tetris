# Roadmap

What is built, what has been checked, and what comes next. Build, run and controls are in `README.md`; module boundaries and conventions are in `ARCHITECTURE.md`.

## Completed milestones

### Milestone 1: `1f9a594` "Render the 10x20 board with SDL3 and OpenGL 3.3"

- SDL3 window with a resizable, high-pixel-density OpenGL 3.3 core context.
- Vendored GLAD loaded through `SDL_GL_GetProcAddress`, so the project does not link `libGL`.
- Instanced quad renderer, playfield frame and grid, and the first `BoardLayout`.
- Fixed 60 Hz loop with an accumulator and a 0.25 s frame clamp.
- Screenshot path and the first board and RNG tests.

The repository itself starts at `99b8f10` "Initialize Tetris project", which only adds `.gitignore` and `opencode.json`. The first feature commit is `1f9a594`.

### Milestone 2: `f007693` "Add falling pieces, keyboard input and line clears"

- `src/core`: board storage, SRS piece shapes, the 7-bag, splitmix64 and `GameState`.
- Gravity, lock delay, DAS/ARR movement and line clears, all counted in ticks.
- `InputBuffer` and scancode-based keyboard mapping.
- Drawing for the active piece, its ghost and the next-piece preview, plus the top-out overlay.
- Gameplay and input-buffer tests.

### Milestone 3: `7652b3e` "Add SRS kicks, hold, scoring, levels and an on-screen HUD"

- SRS wall kicks, hold, a three-piece next queue, the guideline gravity table, scoring and levels.
- Lock-out detection and the per-tick `GAME_EVENT_*` bitmask for presentation.
- Built-in 5x7 pixel font, HOLD and NEXT panels, score/level/lines readout.
- Pause and the paused and game-over overlays.
- Rules tests for kicks, hold, the queue, gravity, scoring, lock out and event bits.

### Milestone 4: `176ca35` "Add line clear animation, particles, block shading, background and sound"

- Line-clear delay in the core: rows are scored at the lock and removed 24 ticks later.
- Block and outline styles, alpha and additive blending, and the animated background shader.
- F5 shader reload and `--assets DIR` for live shader editing.
- `src/fx`: fixed particle pool, trails, lock flash, level-up pulse, screen shake and the clear-animation math.
- Procedural audio: note-list synthesis, a fixed voice mixer, SDL device output, mute and `--mute`.
- Effect and audio tests.

### Milestone 5: "Add a title screen, menus, settings and a save file"

- An app-level screen state machine in `src/app/menu.c`: title, main menu, playing, paused, game over, settings, controls and high scores. `GameState.game_over` stays the rule result; the app mirrors it into the game-over screen.
- A main menu with a start-level picker (1 to 15) wired to `game_init_with_config`.
- Settings for volume, effects intensity, DAS, ARR and two key slots per action, applied at once, also to a paused run.
- `GameConfig` and `game_set_shift_timing` in the core, so DAS and ARR are per run; ARR 0 moves the piece straight to the wall.
- A versioned `key=value` save file with the settings and the top ten scores in `SDL_GetPrefPath`, parsed defensively and replaced through a temporary file and a rename.
- `--play` starts a run without the title, so smoke tests and screenshots of the board keep working.
- Save file parser, serializer, binding and score table tests in `tests/test_save.c`.

### Milestone 6: "Add T-spins, back-to-back, combos and perfect clears"

- T-spin detection with the 3-corner rule in `game_tspin_kind`, with minis and the fifth-kick upgrade. A hard drop in place keeps the spin; any movement after the rotation ends it.
- A scoring table by T-spin kind and line count, back-to-back (1.5 times the line points), combos (50 per step) and perfect clear bonuses, all multiplied by the level before the clear.
- `GameState.last_clear` records what each lock scored; `back_to_back` and `clear_streak` carry the state across locks.
- A clear label in the left panel under the stats, kept and faded by `src/fx`, sized to fit the panel at every window size.
- `tests/test_scoring.c` covers the rule corner by corner, a T-spin double made by a real rotation, minis, back-to-back, combos, perfect clears, the label text and fade, and replay determinism with spins.

## Current status

### What works

- The full game loop: spawn, gravity, soft and hard drop, SRS rotation with kicks, hold, next queue, lock delay, line clears, scoring and levels.
- T-spins and mini T-spins, back-to-back bonuses, combos and perfect clears, with a fading label that names each notable clear.
- Line-clear delay with DAS charging through it, and game over by block out or lock out.
- HUD with hold, next, score, level and lines; pause, resume and restart; focus loss pauses.
- A title screen, a main menu with a start-level picker, pause and game-over menus, a top-ten score table, and settings for volume, effects intensity, DAS, ARR and the key bindings. Settings and scores survive a restart in `save.txt` in SDL's preference directory.
- A neon arcade look: glass blocks lit from inside with a sweeping sheen and random sparkles, a hologram ghost with a beam of light down to it, a glow around the active piece, neon tube frames and glowing text. Also the clear animation, particles, trails, shake, background shader and F5 reload.
- Synthesized sound for moves, rotation, hold, hard drop, lock, line clears, level up, game over and pause. `M` mutes; `--mute` starts muted.
- Command-line flags: `--frames`, `--screenshot`, `--assets`, `--size`, `--seed`, `--mute`, `--play`, `--help`.

### Verified on this machine

- `ctest --preset debug`, `ctest --preset release` and `ctest --preset debug-sanitizers` all pass. Each runs the single `tetris_tests` executable.
- `./build/debug/tetris --frames 2 --screenshot /tmp/opencode/tetris-check.bmp` opened a window and wrote an 800x600 BMP. The log reported an OpenGL 4.6 core context on Mesa Intel Iris Xe and a real audio device opened through PipeWire (`Built-in Audio Analog Stereo`, 48 kHz mono float stream to a 2-channel device).
- The screenshot showed the playfield, the HOLD and NEXT panels, the score/level/lines readout and the shader-drawn background.
- The tests cover the pure core, layout and font math, the input buffer, the effects layer, the audio synth/mixer/event mapping, the save file format and the Milestone 6 scoring. They do not link SDL or OpenGL.
- Each Milestone 6 rule was broken on purpose in a scratch copy (front corners ignored, no fifth-kick upgrade, a hard drop that always ends the spin), and the scoring suite failed every time. The clear label was drawn with all seven lines at 352x352, 800x600 and 1600x1000, forced onto the screen from a throwaway copy of `main.c`; it stayed inside the left panel at all three sizes.
- Milestone 5 was checked with screenshots of every screen at 800x600: title, main menu, settings, controls (including the key prompt), high scores, pause and game over. The menus were driven by key events pushed with `SDL_PushEvent` from a throwaway copy of `main.c`, with `XDG_DATA_HOME` pointed at a scratch directory. Those runs changed the volume and DAS, rebound a key, played runs to game over, and then checked that `save.txt` held the changes and that the next start showed the best score. A save file of random bytes logged one warning and started with the defaults. `packaging/linux/build.sh` still builds and tests the release binary.

### Not verified on this machine

- A T-spin, a perfect clear or a combo made in a real game by a person. The rules are unit tested and the label was drawn on screen, but nobody has played one here.
- Interactive keyboard play by a person. The input path is covered by unit tests, and the menus were driven with synthetic SDL events, but nobody has played through the menus by hand here.
- How the sounds actually sound. The device opened and the synth output is tested for length, peak, finite samples, click-free ends and determinism; nobody has listened to the mix here.
- Windows and macOS builds.
- Live window resize by dragging. The layout is recomputed from the drawable size every frame, but that path has only been exercised through the screenshot size and unit tests.
- Long sessions, high-DPI displays, multiple monitors and audio device changes at runtime.

### Known limitations

- No text beyond the built-in 5x7 font. It covers space, A-Z, 0-9 and `: . - ! ? < >`; lowercase uses the uppercase glyph and other characters are skipped, so a key name such as `[` shows as a gap on the controls screen.
- No music. Mute (`M`) is not saved; only the volume is.
- No player names in the score table, and no way to clear it short of deleting `save.txt`.
- No gamepad, replay or CI. Only Linux is packaged, as a single x86_64 binary built by `packaging/linux/build.sh` and released by `.github/workflows/release.yml`.

### Rough edges

- Visual tuning is compile-time apart from the effects-intensity setting, which scales everything at once. The trail and particle alphas are private constants in `src/render/fx_view.c` (`FX_VIEW_TRAIL_ALPHA`, `FX_VIEW_TRAIL_WIDE_ALPHA`, `FX_VIEW_PARTICLE_ALPHA`), and the shake amounts are in `src/fx/fx.h`. Particle counts round per cell, so the lock dust (two per cell) is gone below effects 3.
- `--seed` is parsed as a non-negative `long` with a maximum of 2147483647, while the core and `RNG_DEFAULT_SEED` use `uint64_t`. The default 64-bit seed cannot be passed back in full on the command line.
- `--play` starts at the saved start level; there is no flag for the level itself.
- At 800x600 the controls screen draws its rows at two font pixels, because a key name like `LEFT SHIFT` has to fit a 30% column. It is readable but small.
- `platform_set_title` is declared and implemented but no longer called; the window title is set once in `platform_init`.
- The loop has no frame limiter of its own. It relies on vsync through `SDL_GL_SetSwapInterval(1)` and only logs a warning when the driver refuses it, so an uncapped driver would spin the loop.
- The move-sound throttle lives in `main.c` (`APP_MOVE_SOUND_TICKS`) rather than in `sound_map`, so the mapping is not fully described by the SDL-free module.
- A screenshot is uncompressed BMP. At 800x600 it is about 1.9 MB.

## Planned milestones

### Milestone 7: presentation polish

**Goal:** make the game look and sound finished without changing the simulation.

**Scope**

- Real font rendering from a texture atlas.
- Row-drop animation after a clear.
- More particle types.
- Post-processing bloom through an offscreen framebuffer.
- Music.

**Design notes**

- The current renderer has no texture support. A font atlas needs a textured quad path, or a second small pipeline; it is not a drop-in replacement for `text_draw`. Pick either a checked-in atlas or a small vendored TrueType rasterizer, and keep the current pixel font as a fallback.
- Row-drop animation must not delay the simulation. The core removes rows on the `GAME_EVENT_CLEAR_DONE` tick. To animate the fall, record the removed rows and enough of the pre-clear board for the view to interpolate, or snapshot the board in the app before the removal tick. The rules and the replay timing stay unchanged.
- Bloom needs an offscreen framebuffer, a downsample/blur pass and an additive composite. It also needs resize handling, because the FBO must follow the drawable size.
- Music needs a voice that the sound-effect pool cannot steal, plus its own volume. The existing mixer has one fixed pool, so either reserve a voice or add a second stream.
- Keep every new effect optional or cheap enough to run on Mesa llvmpipe, which the README lists as a supported fallback.

**Done when**

- Text renders at arbitrary sizes from an atlas, and the HUD remains readable at the minimum supported window size.
- Cleared rows visibly fall after the delay, and the simulation tick and score are unchanged.
- At least two new particle types are in use and tested for lifetime and pool bounds.
- Bloom can be turned off and on, and the frame time on the current software renderer stays acceptable.
- Music loops without clicks, can be muted, and does not steal sound-effect voices.

## Later / ideas

- **Replays.** The core is already deterministic: a seed plus a per-tick input sequence replays a run. Record `held` and `pressed` per tick, add a small replay file format and a playback mode. The main work is a stable encoding and a way to stop or seek, not a change to the rules.
- **Game modes.** Sprint (40 lines) and Ultra (a time limit). Add a mode config to the core with a goal type and a time limit, and show the goal in the HUD.
- **Gamepad support.** Map SDL gamepad events to `GameAction` in `src/app/input_sdl.c`. The core already takes a bitmask, so no rules change is needed.
- **CI.** Build and run the tests on Linux, Windows and macOS. A Linux sanitizer job and a shader-compile smoke test would catch most regressions.
- **Packaging.** Linux ships as a single binary with the shaders built in. Windows and macOS still need their own builds.
