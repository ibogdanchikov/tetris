#include "app/screenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <glad/gl.h>

bool screenshot_save_bmp(int fb_width, int fb_height, const char *path)
{
    if (fb_width <= 0 || fb_height <= 0) {
        fprintf(stderr, "screenshot: invalid framebuffer size %dx%d\n", fb_width, fb_height);
        return false;
    }

    const size_t pitch = (size_t)fb_width * 4;
    const size_t bytes = pitch * (size_t)fb_height;
    unsigned char *pixels = malloc(bytes);
    if (pixels == NULL) {
        fprintf(stderr, "screenshot: out of memory for %zu bytes\n", bytes);
        return false;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)fb_width, (GLsizei)fb_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    /* glReadPixels hands back bottom-up rows; BMP wants them top-down. */
    unsigned char *row_swap = malloc(pitch);
    if (row_swap == NULL) {
        fprintf(stderr, "screenshot: out of memory for row buffer\n");
        free(pixels);
        return false;
    }
    for (int y = 0; y < fb_height / 2; ++y) {
        unsigned char *top = pixels + (size_t)y * pitch;
        unsigned char *bottom = pixels + (size_t)(fb_height - 1 - y) * pitch;
        memcpy(row_swap, top, pitch);
        memcpy(top, bottom, pitch);
        memcpy(bottom, row_swap, pitch);
    }
    free(row_swap);

    SDL_Surface *surface =
        SDL_CreateSurfaceFrom(fb_width, fb_height, SDL_PIXELFORMAT_RGBA32, pixels, (int)pitch);
    if (surface == NULL) {
        fprintf(stderr, "screenshot: SDL_CreateSurfaceFrom: %s\n", SDL_GetError());
        free(pixels);
        return false;
    }

    const bool saved = SDL_SaveBMP(surface, path);
    if (!saved) {
        fprintf(stderr, "screenshot: SDL_SaveBMP(%s): %s\n", path, SDL_GetError());
    }

    /* SDL_SaveBMP has copied the pixels out, so the surface can go now. */
    SDL_DestroySurface(surface);
    free(pixels);

    if (saved) {
        SDL_Log("screenshot written to %s (%dx%d)", path, fb_width, fb_height);
    }
    return saved;
}
