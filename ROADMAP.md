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

## Current status

### What works

- The full game loop: spawn, gravity, soft and hard drop, SRS rotation with kicks, hold, next queue, lock delay, line clears, scoring and levels.
- Line-clear delay with DAS charging through it, and game over by block out or lock out.
- HUD with hold, next, score, level and lines; pause, resume and restart; focus loss pauses.
- A neon arcade look: glass blocks lit from inside with a sweeping sheen and random sparkles, a hologram ghost with a beam of light down to it, a glow around the active piece, neon tube frames and glowing text. Also the clear animation, particles, trails, shake, background shader and F5 reload.
- Synthesized sound for moves, rotation, hold, hard drop, lock, line clears, level up, game over and pause. `M` mutes; `--mute` starts muted.
- Command-line flags: `--frames`, `--screenshot`, `--assets`, `--size`, `--seed`, `--mute`, `--help`.

### Verified on this machine

- `ctest --preset debug`, `ctest --preset release` and `ctest --preset debug-sanitizers` all pass. Each runs the single `tetris_tests` executable.
- `./build/debug/tetris --frames 2 --screenshot /tmp/opencode/tetris-check.bmp` opened a window and wrote an 800x600 BMP. The log reported an OpenGL 4.6 core context on Mesa Intel Iris Xe and a real audio device opened through PipeWire (`Built-in Audio Analog Stereo`, 48 kHz mono float stream to a 2-channel device).
- The screenshot showed the playfield, the HOLD and NEXT panels, the score/level/lines readout and the shader-drawn background.
- The tests cover the pure core, layout and font math, the input buffer, the effects layer and the audio synth/mixer/event mapping. They do not link SDL or OpenGL.

### Not verified on this machine

- Interactive keyboard play by automation. The input path is covered by unit tests, but no test drives a real window with synthetic key events.
- How the sounds actually sound. The device opened and the synth output is tested for length, peak, finite samples, click-free ends and determinism; nobody has listened to the mix here.
- Windows and macOS builds.
- Live window resize by dragging. The layout is recomputed from the drawable size every frame, but that path has only been exercised through the screenshot size and unit tests.
- Long sessions, high-DPI displays, multiple monitors and audio device changes at runtime.

### Known limitations

- No title screen, menu, settings screen or screen state machine. The game starts playing immediately; pause is a `bool` in `main.c`.
- No persistence. There is no settings file and no high-score table; nothing calls `SDL_GetPrefPath`.
- No T-spin, mini T-spin, back-to-back, combo or perfect-clear scoring. `last_kick_index` and `last_action_was_rotation` are recorded but not used by the rules yet.
- No text beyond the built-in 5x7 font. It covers space, A-Z, 0-9 and `: . - ! ?`; lowercase uses the uppercase glyph and other characters are skipped.
- No music, volume control, key rebinding, DAS/ARR settings or start-level picker. DAS and ARR are compile-time constants in `src/core/game.h`.
- No gamepad, replay or CI. Only Linux is packaged, as a single x86_64 binary built by `packaging/linux/build.sh` and released by `.github/workflows/release.yml`.

### Rough edges

- Visual tuning is compile-time. The trail and particle alphas are private constants in `src/render/fx_view.c` (`FX_VIEW_TRAIL_ALPHA`, `FX_VIEW_TRAIL_WIDE_ALPHA`, `FX_VIEW_PARTICLE_ALPHA`), and the shake amounts are in `src/fx/fx.h`. There is no effects-intensity setting.
- `--seed` is parsed as a non-negative `long` with a maximum of 2147483647, while the core and `RNG_DEFAULT_SEED` use `uint64_t`. The default 64-bit seed cannot be passed back in full on the command line.
- `--seed` does not choose a start level. `game_init_at_level` exists in the core, but no flag reaches it. The README sentence that calls `--seed` "the only start-level control" is imprecise.
- `platform_set_title` is declared and implemented but no longer called; the window title is set once in `platform_init`.
- The loop has no frame limiter of its own. It relies on vsync through `SDL_GL_SetSwapInterval(1)` and only logs a warning when the driver refuses it, so an uncapped driver would spin the loop.
- The move-sound throttle lives in `main.c` (`APP_MOVE_SOUND_TICKS`) rather than in `sound_map`, so the mapping is not fully described by the SDL-free module.
- A screenshot is uncompressed BMP. At 800x600 it is about 1.9 MB.

## Planned milestones

### Milestone 5: game flow and persistence

**Goal:** replace the single always-running session with explicit screens, configurable settings and a save file for settings and top-10 scores.

**Scope**

- App-level screen state machine: title, menu, playing, paused, game over. Keep `GameState.game_over` as the rule result and mirror it in the screen layer.
- Settings screen and file: volume, key bindings, DAS/ARR, start level and effects intensity.
- Save file for settings and the top ten scores in `SDL_GetPrefPath`, with a versioned format.
- Start-level picker wired to `game_init_at_level`.
- CLI flags continue to work for smoke tests and screenshots.

**Design notes**

- Keep the core pure. Add a small settings/config struct for DAS, ARR and start level, or pass those values explicitly; `game_init` stays the default wrapper so tests do not need a file. The core never reads a path.
- The save code belongs in `src/app/` because `SDL_GetPrefPath` is SDL. A text format with a magic line, a version number and one `key=value` per line is easier to inspect and migrate than a binary struct. Parse defensively: clamp ranges, ignore unknown keys, and fall back to defaults on a missing or corrupt file.
- Write to a temporary file and rename it, so a crash during save does not destroy the old settings.
- High-score entries need at least score, level, lines and the seed. A date is optional; do not add one unless the screen shows it.
- The screen layer can reuse the existing quad renderer and pixel font. It should not add game rules.

**Done when**

- Launch shows a title screen; the menu starts a game; pause and settings are reachable without restarting the process.
- Settings survive a restart, and the game uses the saved DAS/ARR, volume, start level and effects intensity.
- The top ten scores survive a restart, and a missing or corrupt save file falls back to defaults without crashing.
- Parser and serializer have unit tests that do not link SDL.
- `--frames` and `--screenshot` still work without a save file.

### Milestone 6: scoring extras

**Goal:** add the standard modern scoring events that the current rules omit.

**Scope**

- T-spin detection with the 3-corner rule, using `last_action_was_rotation`, `last_kick_index` and the four corners around the T center.
- Mini T-spins.
- Back-to-back bonuses.
- Combos.
- Perfect clears.
- On-screen labels for these clears.

**Design notes**

- Detect at lock time in `game_lock_piece`, before the rows are removed, and store the result on `GameState` (for example a `last_clear_kind` field). Presentation should read the result, not recompute it.
- The 3-corner rule: at least three of the four diagonal corners around the T center are occupied or outside the board. A mini is the case where the two corners in front of the T are not both filled; some rule sets also require the last kick to be the fifth entry. Put the exact rule in one function and test it directly.
- Add event bits only where the HUD needs a one-shot trigger. Back-to-back and combo counters are state, not events.
- Score with a table, not a chain of `if`s. Keep the level multiplier behavior: points use the level in force before the clear.
- Perfect clear means the board is empty after the rows are removed. Check it after the removal, not at the lock.
- Labels fit the current font: `T-SPIN`, `B2B`, `COMBO` and `PERFECT CLEAR` use only supported characters.

**Done when**

- Known T-spin, mini, B2B, combo and perfect-clear setups score correctly and have tests.
- The HUD shows the right label for each clear, and the label does not overlap the board at small window sizes.
- Replay determinism still holds: the same seed and inputs produce the same score and the same labels.

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
