#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app/input_buffer.h"
#include "app/input_sdl.h"
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

/* The pause blip is an app-level sound, so its gain lives here. */
#define APP_PAUSE_SOUND_GAIN 0.9f

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
    bool show_help;
} AppOptions;

static void app_print_usage(const char *program)
{
    printf("usage: %s [options]\n"
           "\n"
           "  --frames N        render N frames and exit (default: run until closed)\n"
           "  --screenshot PATH write the last rendered frame to PATH as BMP\n"
           "  --assets DIR      load shaders from DIR/shaders (default: next to the binary)\n"
           "  --size WxH        initial window size in pixels (default: %dx%d)\n"
           "  --seed N          simulation seed (default: %llu)\n"
           "  --mute            start with the sound muted (M toggles it)\n"
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
    return true;
}

/*
 * The window title is plain "Tetris"; the score, level, lines and every message
 * now live in the HUD, so there is no title state to keep.
 */

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

    Platform platform;
    if (!platform_init(&platform, APP_TITLE, options.width, options.height)) {
        fprintf(stderr, "error: platform initialisation failed\n");
        return 1;
    }

    Renderer renderer;
    if (!renderer_init(&renderer, shader_dir)) {
        fprintf(stderr, "error: renderer initialisation failed (shaders in %s)\n", shader_dir);
        platform_shutdown(&platform);
        return 1;
    }

    GameState game;
    game_init(&game, options.seed);

    /*
     * Presentation effects keep their own RNG state, seeded from the same base
     * seed but never shared with the game, so particle jitter cannot change the
     * piece order.
     */
    Fx fx;
    fx_init(&fx, options.seed);

    /*
     * Audio is optional: a missing device just logs a warning and every sound
     * call becomes a no-op, so the game always starts.
     */
    Audio audio;
    (void)audio_init(&audio);
    if (options.mute) {
        audio_set_muted(&audio, true);
    }

    InputBuffer input;
    input_buffer_reset(&input);

    /*
     * A restart re-seeds the game from the base seed plus the restart count, so
     * restarts are reproducible but do not replay the same piece order.
     */
    const uint64_t base_seed = options.seed;
    uint32_t restart_count = 0;

    int exit_code = 0;
    long frames_rendered = 0;
    double accumulator = 0.0;
    double last_time = platform_time_seconds();
    bool quit = false;
    bool paused = false;

    /* Ticks of the last move tick sound, so ARR does not machine-gun it. */
    uint64_t last_move_sound_tick = 0;
    bool have_move_sound = false;

    while (!quit) {
        /* Events arrive at frame rate; presses are buffered until a game tick. */
        AppInputCommands commands = { false, false, false, false, false, false };
        SDL_Event event;
        while (platform_next_event(&platform, &event)) {
            app_input_handle_event(&input, &event, &commands);
        }
        if (commands.quit) {
            quit = true;
        }
        if (commands.reload_shaders) {
            renderer_reload_shaders(&renderer);
        }
        if (commands.restart) {
            ++restart_count;
            game_init(&game, base_seed + restart_count);
            fx_reset(&fx);
            audio_stop_all(&audio);
            input_buffer_reset(&input);
            accumulator = 0.0;
            paused = false;
            have_move_sound = false;
        }
        if (commands.mute_toggle) {
            audio_set_muted(&audio, !audio_muted(&audio));
        }

        /*
         * Pause is app state, not game state. Losing focus pauses a running game
         * so the player does not come back to a piece that kept falling; a
         * finished run has nothing to pause. The same blip serves pause and
         * resume, so a toggle always sounds like a toggle.
         */
        if (commands.focus_lost && !game.game_over) {
            if (!paused) {
                audio_play(&audio, SOUND_PAUSE, APP_PAUSE_SOUND_GAIN);
            }
            paused = true;
            input_buffer_reset(&input);
            accumulator = 0.0;
        }
        if (commands.pause_toggle && !game.game_over) {
            paused = !paused;
            audio_play(&audio, SOUND_PAUSE, APP_PAUSE_SOUND_GAIN);
            input_buffer_reset(&input);
            accumulator = 0.0;
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
         * Events are reported per tick, but a frame may run several ticks, so
         * they are OR-ed together for the whole frame. The fx layer consumes the
         * combined bits once, after the ticks.
         */
        uint32_t frame_events = 0;

        if (paused) {
            /* No ticks and no backlog: keys pressed now must not fire on resume. */
            input_buffer_reset(&input);
            accumulator = 0.0;
        } else {
            accumulator += frame_time;
            while (accumulator >= SIM_DT) {
                const GameInput tick_input = input_buffer_take(&input);
                game_step(&game, &tick_input);
                frame_events |= game.events;
                accumulator -= SIM_DT;
            }
        }

        /*
         * Pause freezes the effects with the game: no timers, no particle
         * motion. The shake is not applied while paused either, so the board
         * does not sit at a frozen offset for the whole pause.
         */
        if (!paused) {
            fx_on_events(&fx, frame_events, &game);
            fx_update(&fx, (float)frame_time);

            /*
             * Sounds come from the same OR-ed bits, once per frame. sound_map
             * owns the precedence rules; the only thing left here is the ARR
             * tick, which would otherwise repeat several times a second.
             */
            SoundRequest requests[SOUND_MAP_MAX_REQUESTS];
            const int request_count =
                sound_map_events(frame_events, &game, requests, SOUND_MAP_MAX_REQUESTS);
            for (int i = 0; i < request_count; ++i) {
                if (requests[i].id == SOUND_MOVE) {
                    if (have_move_sound &&
                        game.tick - last_move_sound_tick < APP_MOVE_SOUND_TICKS) {
                        continue;
                    }
                    last_move_sound_tick = game.tick;
                    have_move_sound = true;
                }
                audio_play(&audio, requests[i].id, requests[i].gain);
            }
        }

        const float alpha = (float)(accumulator / SIM_DT);

        int fb_width = 0;
        int fb_height = 0;
        platform_drawable_size(&platform, &fb_width, &fb_height);

        BoardLayout layout = board_layout_compute(fb_width, fb_height);
        if (!paused) {
            float shake_x = 0.0f;
            float shake_y = 0.0f;
            fx_shake_offset(&fx, &shake_x, &shake_y);
            board_layout_translate(&layout, shake_x * (float)layout.cell_size,
                                   shake_y * (float)layout.cell_size);
        }

        renderer_begin(&renderer, fb_width, fb_height);
        /* Presentation-only time, wrapped to an hour so float precision holds. */
        renderer_draw_background(&renderer, (float)SDL_fmod(now, 3600.0));
        board_view_draw(&renderer, &layout, &game, alpha, fx_level_pulse_amount(&fx));
        fx_view_draw(&renderer, &layout, &fx);
        hud_draw(&renderer, &layout, &game, paused);
        renderer_end(&renderer);

        ++frames_rendered;
        const bool last_frame = quit || (options.frames > 0 && frames_rendered >= options.frames);

        /* Read the back buffer before presenting it: after the swap it is undefined. */
        if (last_frame && options.screenshot_path != NULL) {
            if (!screenshot_save_bmp(fb_width, fb_height, options.screenshot_path)) {
                exit_code = 1;
            }
        }

        platform_swap(&platform);

        if (last_frame) {
            break;
        }
    }

    SDL_Log("rendered %ld frames, simulation tick %llu", frames_rendered,
            (unsigned long long)game.tick);

    audio_shutdown(&audio);
    renderer_shutdown(&renderer);
    platform_shutdown(&platform);
    return exit_code;
}
