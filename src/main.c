#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app/input_buffer.h"
#include "app/input_sdl.h"
#include "app/menu.h"
#include "app/save.h"
#include "app/save_file.h"
#include "app/screenshot.h"
#include "app/sound_map.h"
#include "audio/audio.h"
#include "core/game.h"
#include "core/rng.h"
#include "fx/fx.h"
#include "platform/platform.h"
#include "render/board_layout.h"
#include "render/board_view.h"
#include "render/fx_view.h"
#include "render/hud.h"
#include "render/menu_view.h"
#include "render/renderer.h"

#define APP_TITLE "Tetris"
#define APP_WIDTH 800
#define APP_HEIGHT 600

/* Fixed simulation rate; rendering runs as fast as the display allows. */
#define SIM_HZ 60
#define SIM_DT (1.0 / (double)SIM_HZ)

/* Longest frame we accept, so a stall cannot trigger a catch-up spiral. */
#define MAX_FRAME_SECONDS 0.25

#define APP_SEED_DEFAULT RNG_DEFAULT_SEED

/* The pause blip and the menu sounds are app-level sounds, so their gains live here. */
#define APP_PAUSE_SOUND_GAIN 0.9f
#define APP_MENU_MOVE_GAIN 0.8f
#define APP_MENU_SCREEN_GAIN 0.5f

/*
 * ARR repeats GAME_EVENT_MOVE every two ticks, and a tick sound at 30 per
 * second would buzz. Limiting the move sound to this many ticks (about ten a
 * second) keeps it a texture rather than a machine gun.
 */
#define APP_MOVE_SOUND_TICKS 6

typedef struct AppOptions {
    long frames;          /* 0 = run until the window closes */
    const char *screenshot_path;
    const char *assets_dir; /* overrides <base path>/assets, for live shader edits */
    int width;
    int height;
    uint64_t seed;
    bool mute;
    bool play;            /* skip the title and the menu and start a run */
    bool show_help;
} AppOptions;

static void app_print_usage(const char *program)
{
    printf("usage: %s [options]\n"
           "\n"
           "  --frames N        render N frames and exit (default: run until closed)\n"
           "  --screenshot PATH write the last rendered frame to PATH as BMP\n"
           "  --assets DIR      load shaders from DIR/shaders (default: next to the binary,\n"
           "                    or the built-in copies if there is no assets directory)\n"
           "  --size WxH        initial window size in pixels (default: %dx%d)\n"
           "  --seed N          simulation seed (default: %llu)\n"
           "  --mute            start with the sound muted (M toggles it)\n"
           "  --play            start a run right away instead of showing the title\n"
           "  --help            show this message\n",
           program, APP_WIDTH, APP_HEIGHT, (unsigned long long)APP_SEED_DEFAULT);
}

static bool app_parse_long(const char *text, long min, long max, long *out)
{
    char *end = NULL;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        return false;
    }
    *out = value;
    return true;
}

static bool app_parse_size(const char *text, int *out_width, int *out_height)
{
    char *end = NULL;
    errno = 0;
    const long width = strtol(text, &end, 10);
    if (errno != 0 || end == text || (*end != 'x' && *end != 'X') || width < 1 || width > 16384) {
        return false;
    }

    const char *height_text = end + 1;
    errno = 0;
    const long height = strtol(height_text, &end, 10);
    if (errno != 0 || end == height_text || *end != '\0' || height < 1 || height > 16384) {
        return false;
    }

    *out_width = (int)width;
    *out_height = (int)height;
    return true;
}

static bool app_parse_args(int argc, char **argv, AppOptions *options)
{
    options->frames = 0;
    options->screenshot_path = NULL;
    options->assets_dir = NULL;
    options->width = APP_WIDTH;
    options->height = APP_HEIGHT;
    options->seed = APP_SEED_DEFAULT;
    options->mute = false;
    options->play = false;
    options->show_help = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            options->show_help = true;
            return true;
        }
        if (strcmp(arg, "--frames") == 0) {
            if (i + 1 >= argc || !app_parse_long(argv[++i], 1, 10000000, &options->frames)) {
                fprintf(stderr, "error: --frames needs a positive integer\n");
                return false;
            }
        } else if (strcmp(arg, "--screenshot") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --screenshot needs a path\n");
                return false;
            }
            options->screenshot_path = argv[++i];
        } else if (strcmp(arg, "--assets") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --assets needs a directory\n");
                return false;
            }
            options->assets_dir = argv[++i];
        } else if (strcmp(arg, "--size") == 0) {
            if (i + 1 >= argc || !app_parse_size(argv[++i], &options->width, &options->height)) {
                fprintf(stderr, "error: --size needs WxH, for example 1280x720\n");
                return false;
            }
        } else if (strcmp(arg, "--seed") == 0) {
            long seed = 0;
            if (i + 1 >= argc || !app_parse_long(argv[++i], 0, 2147483647L, &seed)) {
                fprintf(stderr, "error: --seed needs a non-negative integer\n");
                return false;
            }
            options->seed = (uint64_t)seed;
        } else if (strcmp(arg, "--mute") == 0) {
            options->mute = true;
        } else if (strcmp(arg, "--play") == 0) {
            options->play = true;
        } else {
            fprintf(stderr, "error: unknown argument %s\n", arg);
            return false;
        }
    }
    return true;
}

/*
 * Shaders live next to the executable (<base path>/assets/shaders) unless
 * --assets names a directory, which lets a developer run from the repo root as
 * ./build/debug/tetris --assets assets and edit shaders in place with F5.
 *
 * Without --assets and without an assets directory next to the executable, the
 * buffer is left empty and the renderer uses the shaders built into the binary.
 * That is what lets the release build be a single downloadable file.
 */
static bool app_shader_dir(const char *assets_dir, char *buffer, size_t buffer_size)
{
    const char *prefix = NULL;
    if (assets_dir != NULL) {
        prefix = assets_dir;
    } else {
        prefix = SDL_GetBasePath();
        if (prefix == NULL) {
            fprintf(stderr, "error: SDL_GetBasePath: %s\n", SDL_GetError());
            return false;
        }
    }

    /* SDL_GetBasePath ends in a separator, a user-supplied --assets may too. */
    char dir[1024];
    const int copied = snprintf(dir, sizeof dir, "%s", prefix);
    if (copied < 0 || (size_t)copied >= sizeof dir) {
        fprintf(stderr, "error: asset directory path is too long\n");
        return false;
    }
    size_t length = strlen(dir);
    while (length > 0 && dir[length - 1] == '/') {
        dir[--length] = '\0';
    }

    const char *suffix = assets_dir != NULL ? "/shaders" : "/assets/shaders";
    const int written = snprintf(buffer, buffer_size, "%s%s", dir, suffix);
    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "error: shader directory path is too long\n");
        return false;
    }

    SDL_PathInfo info;
    if (assets_dir == NULL &&
        (!SDL_GetPathInfo(buffer, &info) || info.type != SDL_PATHTYPE_DIRECTORY)) {
        SDL_Log("No shader directory at %s, using the built-in shaders", buffer);
        buffer[0] = '\0';
    }
    return true;
}


/*
 * Everything one session owns. The window title is plain "Tetris"; the score,
 * level, lines and every message live on screen, so there is no title state.
 */
typedef struct App {
    Platform platform;
    Renderer renderer;
    GameState game;
    Fx fx;
    Audio audio;
    InputBuffer input;
    Menu menu;

    /* Settings and scores, and where they are saved. can_save is false when
       there is no preference directory; the game then runs without saving. */
    SaveData save;
    char save_path[1024];
    bool can_save;
    bool settings_dirty; /* changed since the last save */

    /*
     * Run n is seeded with base_seed + n, so runs are reproducible but do not
     * replay the same piece order. run_seed is the current run's, kept for the
     * score table.
     */
    uint64_t base_seed;
    uint32_t run_count;
    uint64_t run_seed;

    double accumulator;

    /* Tick of the last move tick sound, so ARR does not machine-gun it. */
    uint64_t last_move_sound_tick;
    bool have_move_sound;
} App;

static GameConfig app_game_config(const Settings *settings)
{
    GameConfig config = game_default_config();
    config.start_level = (uint32_t)settings->start_level;
    config.das_ticks = (uint16_t)settings->das_ticks;
    config.arr_ticks = (uint16_t)settings->arr_ticks;
    return config;
}

/*
 * Pushes the settings into the parts that use them. The volume steps are
 * squared, so the low end of the scale is not all one loudness. DAS and ARR
 * also reach the run in progress, so a change made from the pause menu is felt
 * right away.
 */
static void app_apply_settings(App *app)
{
    const Settings *settings = &app->save.settings;
    const float volume = (float)settings->volume / (float)SETTINGS_VOLUME_MAX;
    audio_set_volume(&app->audio, volume * volume);
    fx_set_intensity(&app->fx, (float)settings->effects / (float)SETTINGS_EFFECTS_MAX);
    game_set_shift_timing(&app->game, (uint16_t)settings->das_ticks,
                          (uint16_t)settings->arr_ticks);
}

static void app_save(App *app)
{
    if (app->can_save) {
        (void)save_file_write(app->save_path, &app->save);
    }
    app->settings_dirty = false;
}

/* Starts a new run with the saved start level and shift timing. */
static void app_start_run(App *app)
{
    const GameConfig config = app_game_config(&app->save.settings);
    app->run_seed = app->base_seed + app->run_count;
    ++app->run_count;
    game_init_with_config(&app->game, app->run_seed, &config);
    fx_reset(&app->fx);
    audio_stop_all(&app->audio);
    input_buffer_reset(&app->input);
    app->accumulator = 0.0;
    app->have_move_sound = false;
    app->menu.last_rank = -1;
    menu_set_screen(&app->menu, SCREEN_PLAYING);
}

/* Pause and resume share one blip, so a toggle always sounds like a toggle. */
static void app_pause(App *app)
{
    audio_play(&app->audio, SOUND_PAUSE, APP_PAUSE_SOUND_GAIN);
    menu_set_screen(&app->menu, SCREEN_PAUSED);
    input_buffer_reset(&app->input);
    app->accumulator = 0.0;
}

static void app_resume(App *app)
{
    audio_play(&app->audio, SOUND_PAUSE, APP_PAUSE_SOUND_GAIN);
    menu_set_screen(&app->menu, SCREEN_PLAYING);
    input_buffer_reset(&app->input);
    app->accumulator = 0.0;
}

/* The run ended: record it if it made the table and show the game-over menu. */
static void app_finish_run(App *app)
{
    const HighScore entry = { app->game.score, app->game.level, app->game.lines_cleared,
                              app->run_seed };
    const int rank = save_insert_score(&app->save, &entry);
    if (rank >= 0) {
        app_save(app);
    }
    menu_set_screen(&app->menu, SCREEN_GAME_OVER);
    app->menu.last_rank = rank;
}

/* Acts on what the menu asked for. Returns true when the app should quit. */
static bool app_handle_menu_command(App *app, MenuCommand command)
{
    switch (command) {
    case MENU_COMMAND_MOVED:
        audio_play(&app->audio, SOUND_MOVE, APP_MENU_MOVE_GAIN);
        break;
    case MENU_COMMAND_SCREEN_CHANGED:
        audio_play(&app->audio, SOUND_ROTATE, APP_MENU_SCREEN_GAIN);
        break;
    case MENU_COMMAND_SETTINGS_CHANGED:
        app_apply_settings(app);
        app->settings_dirty = true;
        audio_play(&app->audio, SOUND_MOVE, APP_MENU_MOVE_GAIN);
        break;
    case MENU_COMMAND_START_GAME:
    case MENU_COMMAND_RESTART:
        app_start_run(app);
        break;
    case MENU_COMMAND_RESUME:
        app_resume(app);
        break;
    case MENU_COMMAND_ABANDON:
        /* The run is dropped without a score: only a finished run counts. */
        audio_stop_all(&app->audio);
        fx_reset(&app->fx);
        menu_set_screen(&app->menu, SCREEN_MAIN_MENU);
        audio_play(&app->audio, SOUND_ROTATE, APP_MENU_SCREEN_GAIN);
        break;
    case MENU_COMMAND_QUIT:
        return true;
    case MENU_COMMAND_NONE:
    default:
        break;
    }
    return false;
}

/*
 * Handles one SDL event on whatever screen is showing. Menu keys are applied
 * one event at a time, so a key that opens the key-binding prompt is not itself
 * taken as the key to bind. Returns true when the app should quit.
 */
static bool app_handle_event(App *app, const SDL_Event *event)
{
    AppInputCommands commands;
    memset(&commands, 0, sizeof commands);
    const Screen screen = app->menu.screen;
    const bool was_waiting = app->menu.waiting_for_key;
    app_input_handle_event(&app->input, &app->save.settings, screen, event, &commands);

    bool quit = commands.quit;
    if (commands.reload_shaders) {
        renderer_reload_shaders(&app->renderer);
    }
    if (commands.mute_toggle) {
        audio_set_muted(&app->audio, !audio_muted(&app->audio));
    }

    /* Losing focus pauses a running game, so the player does not come back to
       a piece that kept falling; a finished run has nothing to pause. */
    if (screen == SCREEN_PLAYING && (commands.focus_lost || commands.pause_toggle)) {
        app_pause(app);
        return quit;
    }
    if (screen == SCREEN_PAUSED && commands.pause_toggle) {
        app_resume(app);
        return quit;
    }
    if (commands.restart) {
        app_start_run(app);
        return quit;
    }
    if (screen == SCREEN_PLAYING) {
        return quit;
    }

    if (was_waiting) {
        if (commands.bind_scancode != 0) {
            quit = app_handle_menu_command(
                       app, menu_bind_key(&app->menu, &app->save.settings,
                                          commands.bind_scancode)) ||
                   quit;
        }
        return quit;
    }
    for (int i = 0; i < commands.menu_key_count; ++i) {
        const MenuCommand command =
            menu_handle_key(&app->menu, &app->save.settings, commands.menu_keys[i]);
        quit = app_handle_menu_command(app, command) || quit;
    }
    return quit;
}

/*
 * Runs the ticks the accumulated time pays for while a run is playing, and
 * returns the frame's events OR-ed together: a frame may run several ticks, and
 * the effects and sounds consume the combined bits once.
 */
static uint32_t app_simulate(App *app, double frame_time)
{
    uint32_t frame_events = 0;
    app->accumulator += frame_time;
    while (app->accumulator >= SIM_DT) {
        const GameInput tick_input = input_buffer_take(&app->input);
        game_step(&app->game, &tick_input);
        frame_events |= app->game.events;
        app->accumulator -= SIM_DT;
    }
    return frame_events;
}

/*
 * Sounds come from the frame's OR-ed bits, once per frame. sound_map owns the
 * precedence rules; the only thing left here is the ARR tick, which would
 * otherwise repeat several times a second.
 */
static void app_play_event_sounds(App *app, uint32_t frame_events)
{
    SoundRequest requests[SOUND_MAP_MAX_REQUESTS];
    const int request_count =
        sound_map_events(frame_events, &app->game, requests, SOUND_MAP_MAX_REQUESTS);
    for (int i = 0; i < request_count; ++i) {
        if (requests[i].id == SOUND_MOVE) {
            if (app->have_move_sound &&
                app->game.tick - app->last_move_sound_tick < APP_MOVE_SOUND_TICKS) {
                continue;
            }
            app->last_move_sound_tick = app->game.tick;
            app->have_move_sound = true;
        }
        audio_play(&app->audio, requests[i].id, requests[i].gain);
    }
}

static void app_draw(App *app, float alpha, double now)
{
    int fb_width = 0;
    int fb_height = 0;
    platform_drawable_size(&app->platform, &fb_width, &fb_height);

    /* The shake only moves a game that is running or has just ended, so a
       paused board does not sit at a frozen offset. */
    const Screen screen = app->menu.screen;
    BoardLayout layout = board_layout_compute(fb_width, fb_height);
    if (screen == SCREEN_PLAYING || screen == SCREEN_GAME_OVER) {
        float shake_x = 0.0f;
        float shake_y = 0.0f;
        fx_shake_offset(&app->fx, &shake_x, &shake_y);
        board_layout_translate(&layout, shake_x * (float)layout.cell_size,
                               shake_y * (float)layout.cell_size);
    }

    /* Presentation-only time, wrapped to an hour so float precision holds. */
    const float time_seconds = (float)SDL_fmod(now, 3600.0);
    renderer_begin(&app->renderer, fb_width, fb_height, time_seconds);
    renderer_draw_background(&app->renderer);
    if (!menu_view_hides_game(&app->menu)) {
        board_view_draw(&app->renderer, &layout, &app->game, alpha,
                        fx_level_pulse_amount(&app->fx));
        fx_view_draw(&app->renderer, &layout, &app->fx);
        hud_draw(&app->renderer, &layout, &app->game);
    }
    const MenuViewState view = { &app->menu, &app->save.settings, &app->save, &app->game,
                                 app_input_key_name, time_seconds };
    menu_view_draw(&app->renderer, &layout, &view);
    renderer_end(&app->renderer);
}

int main(int argc, char **argv)
{
    AppOptions options;
    if (!app_parse_args(argc, argv, &options)) {
        app_print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        app_print_usage(argv[0]);
        return 0;
    }

    char shader_dir[1024];
    if (!app_shader_dir(options.assets_dir, shader_dir, sizeof shader_dir)) {
        return 1;
    }

    /* Large state (the particle pool, the voice buffers) lives here, not on the stack. */
    static App app;

    if (!platform_init(&app.platform, APP_TITLE, options.width, options.height)) {
        fprintf(stderr, "error: platform initialisation failed\n");
        return 1;
    }

    if (!renderer_init(&app.renderer, shader_dir[0] != '\0' ? shader_dir : NULL)) {
        fprintf(stderr, "error: renderer initialisation failed (shaders in %s)\n",
                shader_dir[0] != '\0' ? shader_dir : "<built-in>");
        platform_shutdown(&app.platform);
        return 1;
    }

    /* A missing or broken save file is never fatal: the defaults take over. */
    app.can_save = save_file_path(app.save_path, sizeof app.save_path);
    if (app.can_save) {
        save_file_load(app.save_path, &app.save);
    } else {
        save_defaults(&app.save);
    }

    /* A board exists from the start, so every screen has a game to read. */
    app.base_seed = options.seed;
    const GameConfig config = app_game_config(&app.save.settings);
    game_init_with_config(&app.game, app.base_seed, &config);

    /*
     * Presentation effects keep their own RNG state, seeded from the same base
     * seed but never shared with the game, so particle jitter cannot change the
     * piece order.
     */
    fx_init(&app.fx, options.seed);

    /*
     * Audio is optional: a missing device just logs a warning and every sound
     * call becomes a no-op, so the game always starts.
     */
    (void)audio_init(&app.audio);
    if (options.mute) {
        audio_set_muted(&app.audio, true);
    }
    app_apply_settings(&app);

    input_buffer_reset(&app.input);
    menu_init(&app.menu);
    if (options.play) {
        app_start_run(&app);
    }

    int exit_code = 0;
    long frames_rendered = 0;
    double last_time = platform_time_seconds();
    bool quit = false;

    while (!quit) {
        /* Events arrive at frame rate; game presses are buffered until a tick. */
        SDL_Event event;
        while (platform_next_event(&app.platform, &event)) {
            if (app_handle_event(&app, &event)) {
                quit = true;
            }
        }

        const double now = platform_time_seconds();
        double frame_time = now - last_time;
        last_time = now;
        if (frame_time > MAX_FRAME_SECONDS) {
            frame_time = MAX_FRAME_SECONDS;
        } else if (frame_time < 0.0) {
            frame_time = 0.0;
        }

        /*
         * Only a running game ticks. Every other screen drops the backlog and
         * any buffered keys, so nothing pressed in a menu fires on resume.
         * Effects keep moving on the game-over screen, so the last burst
         * finishes; pause freezes them with the game.
         */
        const Screen screen = app.menu.screen;
        if (screen == SCREEN_PLAYING) {
            const uint32_t frame_events = app_simulate(&app, frame_time);
            fx_on_events(&app.fx, frame_events, &app.game);
            fx_update(&app.fx, (float)frame_time);
            app_play_event_sounds(&app, frame_events);
            if (app.game.game_over) {
                app_finish_run(&app);
            }
        } else {
            input_buffer_reset(&app.input);
            app.accumulator = 0.0;
            if (screen == SCREEN_GAME_OVER) {
                fx_update(&app.fx, (float)frame_time);
            }
        }

        /* Settings are written once the player leaves the screens that change
           them, not on every arrow press. */
        if (app.settings_dirty && app.menu.screen != SCREEN_SETTINGS &&
            app.menu.screen != SCREEN_CONTROLS && app.menu.screen != SCREEN_MAIN_MENU) {
            app_save(&app);
        }

        const float alpha =
            app.menu.screen == SCREEN_PLAYING ? (float)(app.accumulator / SIM_DT) : 0.0f;
        app_draw(&app, alpha, now);

        ++frames_rendered;
        const bool last_frame = quit || (options.frames > 0 && frames_rendered >= options.frames);

        /* Read the back buffer before presenting it: after the swap it is undefined. */
        if (last_frame && options.screenshot_path != NULL) {
            int fb_width = 0;
            int fb_height = 0;
            platform_drawable_size(&app.platform, &fb_width, &fb_height);
            if (!screenshot_save_bmp(fb_width, fb_height, options.screenshot_path)) {
                exit_code = 1;
            }
        }

        platform_swap(&app.platform);

        if (last_frame) {
            break;
        }
    }

    if (app.settings_dirty) {
        app_save(&app);
    }

    SDL_Log("rendered %ld frames, simulation tick %llu", frames_rendered,
            (unsigned long long)app.game.tick);

    audio_shutdown(&app.audio);
    renderer_shutdown(&app.renderer);
    platform_shutdown(&app.platform);
    return exit_code;
}
