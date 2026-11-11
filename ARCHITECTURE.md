# Architecture

## Overview

The game is split into a deterministic simulation and a presentation layer that reads it. `src/core` is the whole simulation: no SDL, no OpenGL, no floating point, no dynamic allocation, no clock and no mutable global state. Everything above it either turns events into input for the core or reads the core's state to draw pixels and make sound. A module may include only modules below it in the diagram; nothing below includes anything above.

```
                             tetris (executable)
   main.c, platform.c, audio.c, input_sdl.c, save_file.c, screenshot.c, render/*
                                       |
     +---------------+-----------------+----------------+---------------+
     |               |                 |                |               |
     v               v                 v                v               v
 tetris_fx   tetris_view_layout   tetris_input     tetris_audio     tetris_app
 src/fx      board_layout, font   input_buffer     sound_map,       save, menu
                                                   mix, synth
     +---------------+-----------------+----------------+---------------+
                                       v
                                  tetris_core
                       board, piece, bag, rng, input, game
```

`glad` (the vendored loader) and `SDL3::SDL3` are linked only by the executable. `src/platform/platform.c` is the only code that creates an SDL window or an OpenGL context. `src/audio/audio.c` is the only code that touches SDL audio. `src/app/input_sdl.c`, `src/app/save_file.c` and `src/app/screenshot.c` are app-layer SDL code, `src/render/menu_view.c` uses SDL's `SDL_sinf` and `SDL_fmodf`, and `main.c` calls a few SDL functions directly (`SDL_GetBasePath`, `SDL_fmod`, `SDL_Log`).

## Key decisions

**The core is pure and deterministic.** It uses integer ticks, no SDL or GL headers, no `malloc`, no clock, no `float`/`double` and no mutable globals. `game_init_at_level` starts with `memset(game, 0, sizeof *game)`, so padding is deterministic and two `GameState` values can be compared with `memcmp`. A seed plus an input sequence replays a run exactly.

**The simulation runs at a fixed 60 Hz.** `main.c` accumulates real time, runs `game_step` while the accumulator holds a full `SIM_DT`, and clamps a frame to `MAX_FRAME_SECONDS` (0.25 s) so a stall cannot cause a catch-up spiral. The leftover accumulator becomes `alpha = accumulator / SIM_DT` and is passed to `board_view_draw`; the line-clear animation is the only thing that interpolates with it.

**Input is a per-tick bitmask.** `GameInput` has `held` and `pressed` action bits. The app layer maps scancodes to actions and produces presses, `InputBuffer` remembers them between frames and ticks, and `input_buffer_take` delivers each press to exactly one tick. A press and its release can both happen between two ticks and still move the piece once. DAS and ARR live in the core, not in the input layer.

**Events are per tick, not per frame.** `GameState.events` is a `GAME_EVENT_*` bitmask cleared at the start of every `game_step`. A frame that runs several ticks ORs them together. `main.c` calls `fx_on_events` and `sound_map_events` once per frame with the combined bits.

**Presentation reads state and never writes it.** The views take `const GameState *`. The effects layer has its own `Fx` state and its own splitmix64 generator, so particles cannot change the piece order. Pause is app state, not part of `GameState`.

**Screens are a small state machine outside the game.** `src/app/menu.c` holds the current `Screen` (title, main menu, playing, paused, game over, settings, controls, high scores), the selected row and the key-binding prompt. It takes `MenuKey` values and returns a `MenuCommand` (start a run, resume, restart, drop the run, quit, a setting changed) that `main.c` carries out. It has no SDL and no game rules: `GameState.game_over` stays the rule result, and `main.c` switches to `SCREEN_GAME_OVER` when it sees it. Only `SCREEN_PLAYING` runs ticks; every other screen drops the accumulator and the input buffer each frame, so nothing pressed in a menu reaches the game.

**Settings live outside the core.** The core gets what a run needs through `GameConfig` (start level, DAS, ARR) and keeps DAS and ARR in `GameState`, so the rules never read a file and a state snapshot still replays. Volume goes to `audio_set_volume`, effects intensity to `fx_set_intensity`, and key bindings to `app_input_action`, which looks the scancode up in `Settings`.

**The save file is versioned text.** `src/app/save.c` turns `SaveData` (settings, bindings, top ten scores) into a `TETRIS SAVE` line, `version=1` and one `key=value` per line, and parses it back defensively: no magic line or an unknown version gives the defaults, unknown keys and malformed lines are skipped, numbers are clamped. It works on strings only. `src/app/save_file.c` finds the file with `SDL_GetPrefPath("", "tetris")`, reads it with `SDL_LoadFile`, and writes `<path>.tmp` before renaming it over the old file, so a crash mid-save keeps the previous one. Settings are written when the player leaves the settings screens (or quits), a score as soon as the run ends.

**There is one instanced quad batcher.** `Renderer` queues up to `RENDERER_MAX_RECTS` (4096) quads and draws them with one `glDrawArraysInstanced`. Each instance carries a rectangle, a color and `(style, p0, p1, p2)`. `RECT_STYLE_FLAT` is a plain rectangle, `RECT_STYLE_BLOCK` draws a neon glass block with rounded corners, a glowing tube along the edge, dark glass lit from inside, a thin inner line, a sheen that sweeps across the screen every few seconds and, with a seed in `p1`, random sparkles. `RECT_STYLE_NEON` draws a glowing tube along a rounded rectangle, or a glowing filled one, for the frames, the text glow and the lock flash. `RECT_STYLE_GLOW` draws a radial falloff for particles, the glow around blocks and the active piece's halo, `RECT_STYLE_GHOST` draws one cell of the ghost's hologram silhouette from a mask of its neighbors, and `RECT_STYLE_BEAM` draws the light that falls from the piece to the ghost. The animated parts of the block, ghost and beam styles run on the `u_time` uniform, so `board_view` passes no time of its own. `renderer_set_blend` switches between alpha and additive blending; it flushes the batch first because blending is GL state.

**GLAD is vendored and loaded through SDL.** `gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)` runs in `platform_init` after the context is current. The project never links `libGL`; `libgl-dev` is only needed for the driver headers.

**Shaders are loaded at runtime.** `renderer_init` reads `quad.vert`, `quad.frag`, `background.vert` and `background.frag` from the shader directory. The default is `<SDL_GetBasePath()>/assets/shaders`; `--assets DIR` switches to `DIR/shaders`, and F5 calls `renderer_reload_shaders`. The quad program is required; the background program is optional, and a failed reload keeps the previous program. The same four files are also compiled into the executable: `cmake/embed_shaders.cmake` turns them into a generated `builtin_shaders.c`, and `builtin_shader_source` looks them up by file name. When there is no `--assets` and no `assets/shaders` directory next to the executable, `main.c` passes `NULL` to `renderer_init` and the renderer builds its programs from those copies. That is what lets the Linux release binary (`packaging/linux/build.sh`) be a single file.

**Audio is synthesized at startup.** `src/audio/synth.c` renders every effect from a short note list into mono float buffers. `src/audio/audio.c` opens the default playback device with a mono float32 stream at 48 kHz, mixes up to `MIX_MAX_VOICES` (16) voices in the SDL callback, and soft-clips the sum. The app thread takes `SDL_LockAudioStream` around the voice list and the mute flag; the callback already holds that lock. A missing device is not fatal: it logs one warning, and every call that would produce sound becomes a no-op.

**Y is up everywhere.** Board row 0 is the bottom row. The renderer's pixel origin is the bottom-left corner of the framebuffer. Effect positions are board cells, so `fx_view` scales them with the layout. The SRS piece tables and the wall-kick tables use the same y-up convention; the tests pin the orientation with concrete cases and with a reversibility check.

**Rotation is SRS.** `game_kick_offsets` returns the offsets for one `(from, to)` pair: five entries for I and the 3x3 pieces, one identity entry for O. `game_try_rotate` tries them in order and records the successful index in `last_kick_index`. `tests/test_rules.c` checks that every forward kick list is the negation of the reverse list, plus the cases that would flip if y were down: a vertical I at the left wall lands flat at columns 0-3, a flat I on the floor climbs two rows, and a T under an overhang rotates down two rows.

**Randomness is a 7-bag.** `Bag` holds one permutation of the seven pieces. `bag_next` deals from it and reshuffles when it is empty. The generator state is a single `uint64_t` in `GameState.rng_state`, so a state snapshot is enough to replay a run.

**Gravity is integer.** The increment is in 1/65536 of a row per tick (`GAME_GRAVITY_ONE_ROW`). `GRAVITY_INCREMENT` is built from the guideline formula at 60 ticks per second, level 1 is 1092, and levels above `GAME_MAX_GRAVITY_LEVEL` (20) use the level 20 value. Soft drop is the faster of 20 times the level gravity and `GAME_SOFT_DROP_MIN_INCREMENT` (32768, one row per two ticks).

**Lock delay has a reset budget.** A resting piece gets `GAME_LOCK_DELAY_TICKS` (30) ticks. A successful move or rotation while grounded resets the timer, up to `GAME_MAX_LOCK_RESETS` (15) times. Reaching a row the piece has never been in refreshes both the timer and the budget; leaving the ground without reaching a new row does not.

**Line clears have a delay phase.** Score, line count and `GAME_EVENT_LINE_CLEAR` happen on the lock tick. The full rows stay on the board for `GAME_LINE_CLEAR_DELAY_TICKS` (24) ticks in `GAME_PHASE_CLEARING`; input is ignored except that a held LEFT/RIGHT keeps charging DAS. When the timer runs out, the rows are removed, `GAME_EVENT_CLEAR_DONE` fires and the next piece spawns.

## Modules

| Target | Files | Purpose | May include | Must not include |
| --- | --- | --- | --- | --- |
| `glad` | `external/glad/src/gl.c`, `external/glad/include/` | Generated OpenGL 3.3 core loader. | Its own generated headers | Anything from `src/` |
| `tetris_core` | `src/core/board.c`, `src/core/piece.c`, `src/core/bag.c`, `src/core/rng.c`, `src/core/game.c` | Playfield, SRS shapes, 7-bag, splitmix64 and the tick-based rules. | C standard headers (`stdint.h`, `stdbool.h`, `string.h`) | SDL, GL, allocation, float, clock |
| `tetris_fx` | `src/fx/fx.c` | Particles, effect timers, screen shake and clear-animation math. | `tetris_core` | SDL, GL, allocation, clock |
| `tetris_view_layout` | `src/render/board_layout.c`, `src/render/font.c` | Layout math and the built-in 5x7 font. | `tetris_core` | SDL, GL, allocation |
| `tetris_input` | `src/app/input_buffer.c` | Press/hold buffer between frames and ticks. | `tetris_core` (`src/core/input.h`) | SDL |
| `tetris_audio` | `src/app/sound_map.c`, `src/audio/mix.c`, `src/audio/synth.c` | Event-to-sound mapping, voice mixing and synthesis. | `tetris_core` | SDL, allocation, libm |
| `tetris_app` | `src/app/save.c`, `src/app/menu.c` | Settings, key bindings, the score table, the save text format and the screen state machine. | `tetris_core`, C standard headers (`stdio.h` for `snprintf`) | SDL, GL, file I/O, allocation |
| `tetris` | `src/main.c`, `src/app/input_sdl.c`, `src/app/save_file.c`, `src/app/screenshot.c`, `src/audio/audio.c`, `src/platform/platform.c`, `src/render/board_view.c`, `src/render/fx_view.c`, `src/render/hud.c`, `src/render/menu_view.c`, `src/render/renderer.c`, `src/render/shader.c`, `src/render/text.c`, generated `builtin_shaders.c` | Window, GL context, SDL audio, input mapping, the save file, rendering and the frame loop. | Everything above, `glad`, `SDL3::SDL3` | Nothing forbidden; this is the top layer |
| `tetris_tests` | `tests/test_main.c`, `tests/test_piece.c`, `tests/test_game.c`, `tests/test_rules.c`, `tests/test_input_buffer.c`, `tests/test_font.c`, `tests/test_fx.c`, `tests/test_audio.c`, `tests/test_save.c` | One assert-style test executable. | `tetris_core`, `tetris_fx`, `tetris_input`, `tetris_audio`, `tetris_view_layout`, `tetris_app` | SDL, GL |

`src/render/text.c` is in the executable, not in `tetris_view_layout`, because it draws through `Renderer`. `src/render/board_view.c`, `src/render/fx_view.c`, `src/render/hud.c` and `src/render/menu_view.c` are also executable code: they need GL, even though their layout inputs are pure.

## Main interfaces

### Simulation

```c
GameConfig game_default_config(void);
void game_init_with_config(GameState *game, uint64_t seed, const GameConfig *config);
void game_init(GameState *game, uint64_t seed);
void game_init_at_level(GameState *game, uint64_t seed, uint32_t start_level);
void game_set_shift_timing(GameState *game, uint16_t das_ticks, uint16_t arr_ticks);
void game_step(GameState *game, const GameInput *input);
```

`game_init_with_config` seeds the generator, fills the bag and the next queue, and spawns the first piece at the config's start level (0 is treated as 1) with its DAS and ARR. `game_init` and `game_init_at_level` are wrappers with the default config (level 1, `GAME_DAS_TICKS`, `GAME_ARR_TICKS`). `game_set_shift_timing` changes DAS and ARR mid-run for a settings change from the pause menu. ARR 0 means a repeat moves the piece as far as it goes. `game_step` advances exactly one tick; during play it is the only per-tick entry point that changes the rules state.

```c
typedef enum GameAction {
    ACTION_LEFT = 1u << 0,
    ACTION_RIGHT = 1u << 1,
    ACTION_SOFT_DROP = 1u << 2,
    ACTION_HARD_DROP = 1u << 3,
    ACTION_ROTATE_CW = 1u << 4,
    ACTION_ROTATE_CCW = 1u << 5,
    ACTION_HOLD = 1u << 6,
} GameAction;

typedef struct GameInput {
    uint32_t held;
    uint32_t pressed;
} GameInput;
```

`held` is what is down right now; `pressed` is what went down since the previous tick. The core never sees OS key repeat.

`GameState` fields that presentation reads:

| Field | Meaning and lifetime |
| --- | --- |
| `board`, `piece`, `next_queue`, `has_hold`, `hold_piece`, `hold_used` | Current playfield and pieces. While `phase == GAME_PHASE_CLEARING` there is no active piece; the `piece` fields hold the last locked piece and must not be read as one. |
| `score`, `level`, `lines_cleared`, `start_level` | HUD values. |
| `phase`, `clear_timer`, `cleared_rows`, `cleared_row_count`, `last_clear_count` | `cleared_rows` is valid while the rows are still on the board during `GAME_PHASE_CLEARING` and afterward as a record of what was removed. `last_clear_count` is the number of rows completed by the most recent lock. |
| `last_locked` | The piece as it was when `GAME_EVENT_LOCK` fired. |
| `last_hard_drop_rows`, `last_hard_drop_from` | Set on the `GAME_EVENT_HARD_DROP` tick, before the drop. `rows` is the distance traveled and is 0 when the piece was already resting. |
| `piece_serial` | Incremented on every spawn; renderers can use it to detect a new piece. |
| `tick`, `events`, `game_over`, `game_over_reason` | `events` is cleared at the start of each tick and must be OR-ed across a multi-tick frame. |
| `das_ticks`, `arr_ticks` | The run's shift timing, from `GameConfig`. |

`game_step` order inside one tick: clear events; return early on game over; run the clear-delay phase if clearing; hold on a press; reset `gravity_accum` on a soft-drop press; rotation with kicks (CW and CCW in the same tick cancel); horizontal movement with DAS/ARR; hard drop (fall to the ghost row, score, lock, done); gravity or soft drop; note the lowest row, update `grounded`, run the lock counter and maybe lock; increment `tick`. The order is documented on `game_step` in `src/core/game.h`; the soft-drop accumulator reset is an extra step that happens between hold and rotation.

### Input buffering

```c
void input_buffer_key_down(InputBuffer *buffer, uint32_t action);
void input_buffer_key_up(InputBuffer *buffer, uint32_t action);
GameInput input_buffer_take(InputBuffer *buffer);
void input_buffer_reset(InputBuffer *buffer);
```

`input_buffer_key_down` marks the action held and queues the press. `input_buffer_key_up` clears the held bit but leaves an undelivered press queued. `input_buffer_take` returns one tick of input and clears the queued presses. `input_buffer_reset` drops everything, which is what pause, focus loss, restart and every menu screen do.

### Settings, save file and screens

```c
void settings_defaults(Settings *settings);
uint32_t settings_actions_for_key(const Settings *settings, int scancode);
bool settings_bind_key(Settings *settings, BindAction action, int slot, int scancode);
bool save_parse(const char *text, size_t length, SaveData *out);
size_t save_serialize(const SaveData *save, char *buffer, size_t capacity);
int save_insert_score(SaveData *save, const HighScore *entry);

bool save_file_path(char *buffer, size_t size);
void save_file_load(const char *path, SaveData *out);
bool save_file_write(const char *path, const SaveData *save);

void menu_init(Menu *menu);
MenuCommand menu_handle_key(Menu *menu, Settings *settings, MenuKey key);
MenuCommand menu_bind_key(Menu *menu, Settings *settings, int scancode);
```

`Settings` holds the volume and effects (0 to 10), DAS (1 to 30 ticks), ARR (0 to 10 ticks), the start level (1 to 15) and two key slots per `BindAction`. Keys are SDL scancodes stored as numbers; `save.h` names the ones it needs as USB HID usage IDs, and `input_sdl.c` checks each against `SDL_Scancode` with `_Static_assert`. Escape, P, M, R and F5 are reserved for the app and cannot be bound, and binding a key takes it out of every other slot. `save_parse` always leaves a valid `SaveData` and returns false only when the text is not a save file of a known version. `save_insert_score` keeps the table sorted, puts a tie below the earlier run, ignores a run that scored nothing, and returns the new place or -1.

`menu_handle_key` applies one menu key: up and down move the selection, left and right change a value row or the key slot, select acts on the row, back returns to the screen that opened the current one. `menu_bind_key` takes the key the controls screen is waiting for; Escape cancels it. `main.c` feeds both one SDL event at a time, so the Enter that opens the prompt is not taken as the key to bind.

### Layout

```c
BoardLayout board_layout_compute(int fb_w, int fb_h);
void board_layout_translate(BoardLayout *layout, float dx, float dy);
int board_layout_fit_text_pixel(int width, int glyph_count, int preferred);
```

`board_layout_compute` fits the left panel, the 10x20 playfield and the right panel into the framebuffer with one cell of margin, keeps cells square and integer-sized, and centers the group. `board_layout_translate` moves every rectangle by the same offset; the screen shake uses it. `board_layout_fit_text_pixel` finds the largest glyph pixel size that fits a string in a given width.

### Rendering

```c
bool renderer_init(Renderer *renderer, const char *shader_dir);
void renderer_begin(Renderer *renderer, int fb_w, int fb_h, float time_seconds);
void renderer_draw_background(Renderer *renderer);
void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color);
void renderer_push_styled(Renderer *renderer, float x, float y, float w, float h, Color color,
                          RectStyle style, float p0, float p1, float p2);
void renderer_set_clip(Renderer *renderer, int x, int y, int w, int h);
void renderer_clear_clip(Renderer *renderer);
void renderer_set_blend(Renderer *renderer, RenderBlend blend);
bool renderer_reload_shaders(Renderer *renderer);
void renderer_end(Renderer *renderer);
void renderer_shutdown(Renderer *renderer);
```

`renderer_begin` sets the viewport, clears to the background color, resets the blend mode to alpha and binds the quad program. Its `time_seconds` is presentation-only time; the background and the block sheen read it, the game never does. Rectangles are in pixels with a bottom-left origin. `renderer_end` flushes what is still queued. `renderer_shutdown` is safe to call twice.

```c
void board_view_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
                     float alpha, float frame_pulse);
void fx_view_draw(Renderer *renderer, const BoardLayout *layout, const Fx *fx);
void hud_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game);
void menu_view_draw(Renderer *renderer, const BoardLayout *layout, const MenuViewState *state);
bool menu_view_hides_game(const Menu *menu);
```

Draw order is board, effects, HUD, menu. The title, main menu, high scores, settings and controls cover the whole group with a neon panel, and `menu_view_hides_game` tells `main.c` to skip the board, effects and HUD under them. The pause and game-over menus dim the playfield and leave the HUD visible. Key names on the controls screen come from `app_input_key_name` through a callback in `MenuViewState`, so the view needs no input code. `board_view_draw` draws the neon frame, the grid, the glow of the locked cells and the cells themselves, then either the clear animation while the phase is clearing or, with additive blending, the drop beam, the ghost and the halo, followed by the active piece with alpha blending. `fx_view_draw` draws the lock flash, trails and particles; it switches to additive blending for the trails and particles and restores alpha before returning. `hud_draw` draws the hold and next panels with the same neon frame (`board_view_draw_neon_frame`) and the stats; `text_draw` adds the glow behind the letters in an additive pass of its own.

### Effects

```c
void fx_init(Fx *fx, uint64_t seed);
void fx_reset(Fx *fx);
void fx_on_events(Fx *fx, uint32_t frame_events, const GameState *game);
void fx_update(Fx *fx, float dt);
void fx_set_intensity(Fx *fx, float intensity);
void fx_shake_offset(const Fx *fx, float *out_x, float *out_y);
void fx_clear_visual(float progress, FxClearVisual *out);
```

`fx_on_events` consumes the frame's OR-ed event bits and the game snapshots. `fx_set_intensity` (0 to 1, kept by `fx_reset`) scales the particle counts per cell, the shake, the lock flash, the trails and the level-up pulse; at 1 everything is exactly as tuned, at 0 none of it happens. The line-clear animation is not scaled. `fx_update` advances timers and particles by `dt`, clamped to `FX_MAX_DT` (0.1 s). `fx_shake_offset` returns the current shake in board cells. `fx_clear_visual` maps normalized clear progress to the flash amount and per-column visibility used by `board_view`.

### Audio

```c
bool audio_init(Audio *audio);
void audio_play(Audio *audio, SoundId id, float gain);
void audio_stop_all(Audio *audio);
void audio_set_muted(Audio *audio, bool muted);
bool audio_muted(const Audio *audio);
void audio_set_volume(Audio *audio, float volume);
void audio_shutdown(Audio *audio);

int sound_map_events(uint32_t events, const GameState *game, SoundRequest out[], int max);
int synth_render(SoundId id, int sample_rate, float *out, int capacity);
```

`audio_init` is the only place that opens the SDL audio device. `audio_set_volume` multiplies `AUDIO_MASTER_GAIN` by 0 to 1 and fades like mute; `main.c` squares the 0 to 10 setting so the low end is not one loudness. It returns false after logging a warning, and later calls that would produce sound become no-ops. `sound_map_events` turns event bits into at most `SOUND_MAP_MAX_REQUESTS` (8) requests and owns the precedence rules (a clear replaces the lock click, a hard drop replaces it too, game over suppresses the lock and clear sounds). `synth_render` is deterministic and SDL-free.

### Platform

```c
bool platform_init(Platform *platform, const char *title, int width, int height);
bool platform_next_event(Platform *platform, SDL_Event *out_event);
void platform_drawable_size(Platform *platform, int *out_width, int *out_height);
void platform_swap(Platform *platform);
void platform_shutdown(Platform *platform);
double platform_time_seconds(void);
```

`platform_next_event` pops raw SDL events and knows nothing about game actions. `platform_drawable_size` returns the framebuffer size, which can differ from the window size on HiDPI. `platform_time_seconds` is monotonic.

## Frame and tick flow

Startup loads the save file (or the defaults), starts a board so every screen has a game to read, applies the settings to audio, effects and the game, and shows the title, or starts a run at once with `--play`.

One iteration of the loop in `main.c`:

1. Drain SDL events with `platform_next_event` and handle each one in `app_handle_event`. `app_input_handle_event` turns it into `AppInputCommands` for the current screen. While playing, game keys go into `InputBuffer` through the bindings, and Escape or P pause. On other screens the arrows, Return, Escape, Backspace and Delete become `MenuKey` values, which go to `menu_handle_key` (or the key goes to `menu_bind_key` while the controls screen waits for one). R restarts while playing, paused or after game over, M toggles mute, F5 reloads the shaders, and a focus loss pauses a running game. The returned `MenuCommand` starts a run (`base_seed + run count`, the saved start level, DAS and ARR), resumes, drops the run, applies a setting or quits.
2. Read `platform_time_seconds`, compute `frame_time`, and clamp it to `MAX_FRAME_SECONDS`. A negative frame time becomes 0.
3. On `SCREEN_PLAYING`, add `frame_time` to the accumulator and, while it holds a full `SIM_DT`, call `input_buffer_take`, then `game_step`, and OR `game.events` into `frame_events`. Then `fx_on_events`, `fx_update`, and the sounds from `sound_map_events`, dropping move sounds that arrive less than `APP_MOVE_SOUND_TICKS` (6) ticks after the last one. If the game is now over, insert the run into the score table, save if it placed, and switch to `SCREEN_GAME_OVER`.
4. On any other screen, reset the input buffer and the accumulator so no key pressed now fires later. The game-over screen keeps calling `fx_update`, so the last burst finishes; pause freezes the effects with the game.
5. If settings changed and the player has left the settings, controls and main menu screens, write the save file.
6. Compute `alpha` (0 outside play), read the drawable size, compute the layout, and translate it by the shake offset times the cell size while playing or on the game-over screen.
7. `renderer_begin` with `SDL_fmod(now, 3600.0)`, `renderer_draw_background`, then `board_view_draw`, `fx_view_draw` and `hud_draw` unless `menu_view_hides_game`, then `menu_view_draw`, `renderer_end`.
8. On the last frame, call `screenshot_save_bmp` before `platform_swap`, because the back buffer is undefined after the swap. Then swap.
9. After the loop, write the save file if a setting is still unsaved, then shut down audio, renderer and platform.

## Conventions

- C17 with extensions off (`CMAKE_C_EXTENSIONS OFF`). No compiler-specific extensions in project code.
- `#pragma once` in every header. Function and variable names are `snake_case`. Public functions carry their module prefix (`game_`, `board_`, `fx_`, `renderer_`, `audio_`, `platform_`). Structs and enums are `typedef`ed (`GameState`, `BoardLayout`, `RectStyle`). Constants are `UPPER_CASE` with a module prefix (`GAME_LOCK_DELAY_TICKS`, `FX_MAX_PARTICLES`, `RENDERER_MAX_RECTS`).
- File-local helpers are `static`. Headers expose only what another module needs.
- Pointers are `const` when the callee does not write through them. Views take `const GameState *`; the core takes `GameState *`.
- Functions that can fail return `bool` or a count. They log the reason (`SDL_Log` in SDL code, `fprintf(stderr, ...)` elsewhere), undo what they allocated and return a value the caller can check. `main` turns a failed init into a non-zero exit code.
- Allocation belongs to the executable. The core, `fx`, the layout library, the input buffer, the synth, the mixer, the sound map, the save format and the menus never allocate. `save_file.c` frees the buffer `SDL_LoadFile` returns. `Renderer` allocates its instance array in `renderer_init` and frees it in `renderer_shutdown`; `audio.c` allocates the pre-rendered sound buffers and the scratch buffer and frees them in `audio_shutdown`; `screenshot.c` allocates the pixel buffer for one frame and frees it.
- Warnings are on for project code and off for generated GLAD code. GCC/Clang get `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition`; MSVC gets `/W4`. `TETRIS_WARNINGS_AS_ERRORS=ON` adds `-Werror` or `/WX`.
- `TETRIS_ENABLE_SANITIZERS=ON` adds `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all` and the matching link options. It is ignored on MSVC with a warning.
- Tests are one executable with a small assert-style harness. `CHECK` and `CHECK_EQ_INT` print the file and line and increment `test_failures`; `main` returns that count. Tests link `tetris_core`, `tetris_fx`, `tetris_input`, `tetris_audio`, `tetris_view_layout` and `tetris_app`, never SDL or GL.
- Tuning constants live next to the code that owns them: rules in `src/core/game.h`, effect lifetimes and shake in `src/fx/fx.h`, layout in `src/render/board_layout.h`, renderer capacity and styles in `src/render/renderer.h`, audio format and master gain in `src/audio/audio.h`, synthesis limits in `src/audio/synth.h`, setting ranges and the save format version in `src/app/save.h`, and app timing and menu sound gains in `src/main.c`. Private visual constants such as the trail alpha live at the top of `src/render/fx_view.c`.

### Adding a sound

1. Add an id to `SoundId` in `src/audio/synth.h`.
2. Add a note table in `src/audio/synth.c`, a name in `synth_name`, and the length/table wiring so `synth_length` and `synth_render` handle it.
3. Map the event bit in `src/app/sound_map.c` and decide its precedence.
4. Add a check to `tests/test_audio.c` for length, peak, finite samples, click-free ends and determinism.

### Adding an effect

1. Add state or constants to `src/fx/fx.h`.
2. Spawn it in `fx_on_events` from the relevant event bits and game snapshot.
3. Advance it in `fx_update` and expose any amount the view needs.
4. Draw it in `src/render/fx_view.c` using the layout, and restore blend and clip state.
5. Add a test to `tests/test_fx.c` for the spawn condition, the lifetime and the deterministic result.

### Adding a shader style

1. Add an id to `RectStyle` in `src/render/renderer.h`.
2. Add the matching branch and constants to `assets/shaders/quad.frag`. The style id in the shader and the enum must stay in step.
3. Pass the style parameters through `renderer_push_styled` and document what `p0`, `p1` and `p2` mean.
4. Check the style at more than one window size; the shader works in pixels, not cells.

### Adding a setting

1. Add the field and its range to `Settings` in `src/app/save.h`, its default to `settings_defaults` and its clamp to `settings_clamp`.
2. Read and write it in `save_apply_line` and `save_serialize`. A new key needs no version bump, because older builds skip unknown keys; a changed meaning does.
3. Add a `MenuItem`, put it in the screen's item list in `src/app/menu.c`, and give it a label, a value and a range in `menu_item_label`, `menu_item_value` and `menu_item_setting`.
4. Apply it in `app_apply_settings` in `src/main.c`.
5. Extend the round trip and the clamping checks in `tests/test_save.c`.

### Adding a game rule

1. Put constants and any new `GameState` fields in `src/core/game.h`, with the tick unit and the valid range in the comment.
2. Implement it in `src/core/game.c` inside `game_step` or a `static` helper. Keep it integer and free of SDL, GL and allocation.
3. Add a `GAME_EVENT_*` bit only if presentation needs to react; otherwise the view can read the state.
4. Add tests to `tests/test_game.c` or `tests/test_rules.c`, including the boundary and the replay-determinism case.

## Build and tooling

Presets are `debug`, `release` and `debug-sanitizers`; each configures and builds into `build/<preset>`. `cmake --preset debug`, `cmake --build --preset debug`, and `ctest --preset debug` are the normal commands. The executable is `build/<preset>/tetris`; the test binary is `build/<preset>/tetris_tests`.

The `tetris` target has a post-build step that copies `assets/` next to the executable, so the binary finds its shaders from any working directory. `--assets DIR` overrides that for live shader editing.

GLAD is checked in. To regenerate it, use the command in `external/glad/README.md`; the project does not need Python or network access to build.

The `debug-sanitizers` preset is separate from `debug` on purpose. Under a Wayland session, SDL's libdecor/GTK decoration libraries can leak at exit, so LeakSanitizer makes the game exit non-zero with reports that point at GTK, pango and fontconfig rather than this project. The test binary is clean under ASan and UBSan.

See `README.md` for requirements, build commands, controls and command-line flags.
