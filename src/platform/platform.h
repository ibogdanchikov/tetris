#pragma once

#include <stdbool.h>

#include <SDL3/SDL.h>

/* SDL window plus OpenGL context. The GL functions themselves live in glad. */
typedef struct Platform {
    SDL_Window *window;
    SDL_GLContext gl_context;
} Platform;

/*
 * Creates a resizable, high-pixel-density window with an OpenGL 3.3 core
 * profile context, makes it current, loads GL through glad and enables vsync
 * when the driver allows it. Returns false on any failure.
 */
bool platform_init(Platform *platform, const char *title, int width, int height);

/*
 * Pops one event from the queue into out_event and returns true. Returns false
 * once the queue is empty, which leaves the meaning of the events to the app:
 * this layer deliberately knows nothing about game actions or restart keys.
 */
bool platform_next_event(Platform *platform, SDL_Event *out_event);

/* Sets the window title. Logs a warning if SDL refuses it. */
void platform_set_title(Platform *platform, const char *title);

/* Framebuffer size in pixels, which may exceed the window size on HiDPI. */
void platform_drawable_size(Platform *platform, int *out_width, int *out_height);

void platform_swap(Platform *platform);

void platform_shutdown(Platform *platform);

/* Monotonic time in seconds. */
double platform_time_seconds(void);
