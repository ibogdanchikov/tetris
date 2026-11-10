#include "platform/platform.h"

#include <stddef.h>

#include <glad/gl.h>

static bool platform_fail(const char *what)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", what, SDL_GetError());
    return false;
}

static bool platform_set_gl_attributes(void)
{
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) ||
        !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
        !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0) ||
        !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0)) {
        return platform_fail("SDL_GL_SetAttribute");
    }
    return true;
}

static void platform_log_gl_info(void)
{
    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);

    SDL_Log("OpenGL version:  %s", version != NULL ? (const char *)version : "<unknown>");
    SDL_Log("OpenGL renderer: %s", renderer != NULL ? (const char *)renderer : "<unknown>");
    SDL_Log("GLSL version:    %s", glsl != NULL ? (const char *)glsl : "<unknown>");
}

bool platform_init(Platform *platform, const char *title, int width, int height)
{
    platform->window = NULL;
    platform->gl_context = NULL;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return platform_fail("SDL_Init");
    }
    if (!platform_set_gl_attributes()) {
        platform_shutdown(platform);
        return false;
    }

    const SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    platform->window = SDL_CreateWindow(title, width, height, flags);
    if (platform->window == NULL) {
        platform_fail("SDL_CreateWindow");
        platform_shutdown(platform);
        return false;
    }

    platform->gl_context = SDL_GL_CreateContext(platform->window);
    if (platform->gl_context == NULL) {
        platform_fail("SDL_GL_CreateContext");
        platform_shutdown(platform);
        return false;
    }
    if (!SDL_GL_MakeCurrent(platform->window, platform->gl_context)) {
        platform_fail("SDL_GL_MakeCurrent");
        platform_shutdown(platform);
        return false;
    }

    if (gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "gladLoadGL failed");
        platform_shutdown(platform);
        return false;
    }
    platform_log_gl_info();

    if (!SDL_GL_SetSwapInterval(1)) {
        /* A driver without vsync support is fine: we just run uncapped. */
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "vsync unavailable: %s", SDL_GetError());
    }

    return true;
}

bool platform_poll_events(Platform *platform)
{
    (void)platform;
    bool quit = false;
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            quit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE) {
                quit = true;
            }
            break;
        default:
            break;
        }
    }
    return quit;
}

void platform_drawable_size(Platform *platform, int *out_width, int *out_height)
{
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(platform->window, &width, &height)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetWindowSizeInPixels: %s", SDL_GetError());
    }
    *out_width = width > 0 ? width : 1;
    *out_height = height > 0 ? height : 1;
}

void platform_swap(Platform *platform)
{
    if (!SDL_GL_SwapWindow(platform->window)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_SwapWindow: %s", SDL_GetError());
    }
}

void platform_shutdown(Platform *platform)
{
    if (platform->gl_context != NULL) {
        SDL_GL_DestroyContext(platform->gl_context);
        platform->gl_context = NULL;
    }
    if (platform->window != NULL) {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
}

double platform_time_seconds(void)
{
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}
