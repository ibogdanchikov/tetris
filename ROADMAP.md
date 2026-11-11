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

### Milestone 7, part 1: "Let the rows above a clear fall into place"

- On `GAME_EVENT_CLEAR_DONE` the fx layer copies the removed rows and starts a 0.15 s timer; `fx_row_drop_offset` gives each row's height above its place, easing from the rows it fell to 0.
- `board_view` draws the moved rows that high, with an empty slot under them, clipped at the top of the playfield. `board_view_draw` takes the `Fx` instead of the frame pulse.
- The core is untouched: rows are removed on the same tick, and the score and the replay timing are the same.

### Milestone 7, part 2: "Add bloom"

- With bloom on, the frame is drawn into an offscreen texture that follows the drawable size; `renderer_end` extracts the bright parts at a quarter of the size, blurs them twice along x and y, and adds them back over the scene.
- Three new shaders, `bloom_extract.frag`, `bloom_blur.frag` and `bloom_composite.frag`, drawn with the full-screen `background.vert` and built into the executable like the others. F5 reloads them.
- A **BLOOM** setting (on by default), saved as `bloom=` in the save file; the existing round-trip test covers it.
- Without the shaders or with incomplete framebuffers the game logs once and draws without bloom.

### Milestone 7, part 3: "Add hard-drop sparks and shockwave rings"

- Particles get a kind. `FX_PARTICLE_SPARK` comes from the bottom cells of a hard-dropped piece (`FX_SPARKS_PER_CELL` each), faster for longer drops, under stronger gravity, and is drawn as a short streak pushed towards white.
- `FxRing` shockwaves come from big clears (four lines, a T-spin with lines, a perfect clear, which gets a second wider ring), in their own pool of `FX_MAX_RINGS`, drawn as a growing neon circle.
- Both follow the effects intensity.
- `tests/test_fx.c` covers their spawn counts and placement, nothing with the effects off, lifetimes, spark gravity, ring growth and both pool bounds.

### Milestone 7, part 4: "Add music"

- An original 16-second loop, 8 bars at 120 BPM in A minor, synthesized at startup by `synth_music_render` from a chord per bar and a melody table. Notes that run past the end are written at the start, so the loop has no seam.
- `MixLoop` and `mix_add_loop`: a looping voice outside the effect pool, so effects never steal it and restarts do not stop it. It plays under the master fade, so the volume and mute apply.
- A **MUSIC** setting (0 to 10, default 7) on top of the volume, faded per block like mute.
- Tests for the loop's length, peak, determinism and seam, the looping mixer, and the music beside a full effect pool.

## Current status

### What works

- The full game loop: spawn, gravity, soft and hard drop, SRS rotation with kicks, hold, next queue, lock delay, line clears, scoring and levels.
- T-spins and mini T-spins, back-to-back bonuses, combos and perfect clears, with a fading label that names each notable clear.
- After a clear, the rows above fall into place instead of jumping.
- Bloom over the whole picture, switchable in the settings.
- Hard-drop sparks and shockwave rings for big clears.
- Background music on its own voice, with its own level.
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
- Bloom was captured on and off, with three threshold and strength pairs and two buffer sizes compared side by side before picking 0.75 and 1.2 at a quarter of the size. The single-file release binary built bloom from its built-in shaders. Frame times from a throwaway build that timed frames 30 to 150: with llvmpipe, 18.6 ms off and 25.5 to 27.0 ms on at 800x600, 39 ms off and 57 ms on at 1920x1080. On the Iris Xe both stayed at the 16.7 ms vsync interval at 1920x1080 (vsync could not be turned off on Wayland, so the true GPU cost is not known).
- Window resizing with bloom on. The buffers are rebuilt on every size change, and every run here started at a new size, but nobody has dragged a window edge.
- Sparks and rings in a real game. They were drawn from a scratch build that fed a hard drop and a perfect four-line clear into the fx layer and captured them at about 30, 60, 100 and 175 ms; the first sparks were too faint and were made larger and brighter after that.
- The row drop at full speed in a real game. Its offsets were checked with a throwaway program (clears of rows 0-1, 2, 1 and 3, and 0-3, plus a lock in the same frame) and drawn from a scratch build pinned at the start, middle and end of the fall, but not watched in motion.
- Interactive keyboard play by a person. The input path is covered by unit tests, and the menus were driven with synthetic SDL events, but nobody has played through the menus by hand here.
- How the sounds and the music actually sound. The device opened and the synth output is tested for length, peak, finite samples, click-free ends and determinism; nobody has listened to the mix here. The music was checked by analysis instead: a throwaway program measured a seam step of 0.00005 against 0.064 for the largest step inside the loop, a peak of 0.51, no DC offset and even loudness across the eight bars, and an ffmpeg spectrogram showed the melody, arpeggio, bass and drums where they were written. The seam test was checked against a render that cut the notes at the end, which it fails. The sanitizer build ran 240 frames with the music mixing through SDL's dummy audio driver without a report. Its level against the effects (`AUDIO_MUSIC_GAIN` 0.5, MUSIC 7 by default) is a guess until someone listens.
- Windows and macOS builds.
- Live window resize by dragging. The layout is recomputed from the drawable size every frame, but that path has only been exercised through the screenshot size and unit tests.
- Long sessions, high-DPI displays, multiple monitors and audio device changes at runtime.

### Known limitations

- No text beyond the built-in 5x7 font. It covers space, A-Z, 0-9 and `: . - ! ? < >`; lowercase uses the uppercase glyph and other characters are skipped, so a key name such as `[` shows as a gap on the controls screen.
- One music track, looping the whole time, the same in menus and in play. Mute (`M`) is not saved; only the volume and the music level are.
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
- Bloom is on by default on every renderer, by choice: with llvmpipe it costs about 7 ms a frame at 800x600 and 18 ms at 1920x1080, and a player who wants the frames back turns it off in the settings. The game does not detect llvmpipe to pick another default.

## Planned milestones

### Milestone 7: presentation polish

**Goal:** make the game look and sound finished without changing the simulation. The row-drop animation (part 1), bloom (part 2), the new particle types (part 3) and music (part 4) are done; the atlas font is what is left.

**Scope**

- Real font rendering from a texture atlas.

**Design notes**

- The current renderer has no texture support. A font atlas needs a textured quad path, or a second small pipeline; it is not a drop-in replacement for `text_draw`. Pick either a checked-in atlas or a small vendored TrueType rasterizer, and keep the current pixel font as a fallback.
- Keep every new effect optional or cheap enough to run on Mesa llvmpipe, which the README lists as a supported fallback.

**Done when**

- Text renders at arbitrary sizes from an atlas, and the HUD remains readable at the minimum supported window size.

## Later / ideas

- **Replays.** The core is already deterministic: a seed plus a per-tick input sequence replays a run. Record `held` and `pressed` per tick, add a small replay file format and a playback mode. The main work is a stable encoding and a way to stop or seek, not a change to the rules.
- **Game modes.** Sprint (40 lines) and Ultra (a time limit). Add a mode config to the core with a goal type and a time limit, and show the goal in the HUD.
- **Gamepad support.** Map SDL gamepad events to `GameAction` in `src/app/input_sdl.c`. The core already takes a bitmask, so no rules change is needed.
- **CI.** Build and run the tests on Linux, Windows and macOS. A Linux sanitizer job and a shader-compile smoke test would catch most regressions.
- **Packaging.** Linux ships as a single binary with the shaders built in. Windows and macOS still need their own builds.
