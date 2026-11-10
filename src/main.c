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
#include "core/game.h"
#include "core/rng.h"
#include "platform/platform.h"
#include "render/board_layout.h"
#include "render/board_view.h"
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

typedef struct AppOptions {
    long frames;          /* 0 = run until the window closes */
    const char *screenshot_path;
    int width;
    int height;
    uint64_t seed;
    bool show_help;
} AppOptions;

static void app_print_usage(const char *program)
{
    printf("usage: %s [options]\n"
           "\n"
           "  --frames N        render N frames and exit (default: run until closed)\n"
           "  --screenshot PATH write the last rendered frame to PATH as BMP\n"
           "  --size WxH        initial window size in pixels (default: %dx%d)\n"
           "  --seed N          simulation seed (default: %llu)\n"
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
    options->width = APP_WIDTH;
    options->height = APP_HEIGHT;
    options->seed = APP_SEED_DEFAULT;
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
        } else {
            fprintf(stderr, "error: unknown argument %s\n", arg);
            return false;
        }
    }
    return true;
}

/* Shaders live next to the executable: <base path>/assets/shaders. */
static bool app_shader_dir(char *buffer, size_t buffer_size)
{
    const char *base = SDL_GetBasePath();
    if (base == NULL) {
        fprintf(stderr, "error: SDL_GetBasePath: %s\n", SDL_GetError());
        return false;
    }
    const int written = snprintf(buffer, buffer_size, "%sassets/shaders", base);
    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "error: shader directory path is too long\n");
        return false;
    }
    return true;
}

/*
 * The window title is the only text this milestone has: without a font renderer
 * it still tells the player the line count and that the game is over. The title
 * is rebuilt only when lines_cleared or topped_out actually change, so it does
 * not churn the window manager every frame.
 */
static void app_update_title(Platform *platform, const GameState *game, uint32_t *shown_lines,
                             bool *shown_topped_out)
{
    if (*shown_lines == game->lines_cleared && *shown_topped_out == game->topped_out) {
        return;
    }
    *shown_lines = game->lines_cleared;
    *shown_topped_out = game->topped_out;

    char title[64];
    if (game->topped_out) {
        snprintf(title, sizeof title, "%s — game over (R to restart)", APP_TITLE);
    } else {
        snprintf(title, sizeof title, "%s — lines %u", APP_TITLE,
                 (unsigned)game->lines_cleared);
    }
    platform_set_title(platform, title);
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
    if (!app_shader_dir(shader_dir, sizeof shader_dir)) {
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

    InputBuffer input;
    input_buffer_reset(&input);

    /*
     * A restart re-seeds the game from the base seed plus the restart count, so
     * restarts are reproducible but do not replay the same piece order.
     */
    const uint64_t base_seed = options.seed;
    uint32_t restart_count = 0;

    /* Sentinel so the first frame writes the title once. */
    uint32_t shown_title_lines = UINT32_MAX;
    bool shown_title_topped_out = false;

    int exit_code = 0;
    long frames_rendered = 0;
    double accumulator = 0.0;
    double last_time = platform_time_seconds();
    bool quit = false;

    while (!quit) {
        /* Events arrive at frame rate; presses are buffered until a game tick. */
        AppInputCommands commands = { false, false };
        SDL_Event event;
        while (platform_next_event(&platform, &event)) {
            app_input_handle_event(&input, &event, &commands);
        }
        if (commands.quit) {
            quit = true;
        }
        if (commands.restart) {
            ++restart_count;
            game_init(&game, base_seed + restart_count);
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

        accumulator += frame_time;
        while (accumulator >= SIM_DT) {
            const GameInput tick_input = input_buffer_take(&input);
            game_step(&game, &tick_input);
            accumulator -= SIM_DT;
        }
        const float alpha = (float)(accumulator / SIM_DT);

        int fb_width = 0;
        int fb_height = 0;
        platform_drawable_size(&platform, &fb_width, &fb_height);

        const BoardLayout layout = board_layout_compute(fb_width, fb_height);
        renderer_begin(&renderer, fb_width, fb_height);
        board_view_draw(&renderer, &layout, &game, alpha);
        renderer_end(&renderer);

        app_update_title(&platform, &game, &shown_title_lines, &shown_title_topped_out);

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

    renderer_shutdown(&renderer);
    platform_shutdown(&platform);
    return exit_code;
}
