#pragma once

#include <stdbool.h>

#include <glad/gl.h>

typedef struct Color {
    float r;
    float g;
    float b;
    float a;
} Color;

/* Fixed capacity of the per-frame rectangle batch. */
#define RENDERER_MAX_RECTS 4096

typedef struct Renderer {
    GLuint program;
    GLuint vao;
    GLuint quad_vbo;
    GLuint instance_vbo;
    GLint viewport_uniform;
    int capacity;
    int count;
    float *instances; /* capacity * RENDERER_INSTANCE_FLOATS floats */
} Renderer;

/*
 * Creates the shader program (shader_dir must contain quad.vert and quad.frag)
 * and the instanced quad geometry. Returns false on any failure.
 */
bool renderer_init(Renderer *renderer, const char *shader_dir);

/*
 * Starts a frame: sets the viewport and clears to the background colour.
 * All rectangles pushed afterwards are in pixel coordinates with the origin in
 * the bottom-left corner of the framebuffer.
 */
void renderer_begin(Renderer *renderer, int fb_w, int fb_h);

/* Queues one axis-aligned rectangle. Flushes early if the batch is full. */
void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color);

/*
 * Restricts everything drawn from now on to the given pixel rectangle, in the
 * same bottom-left-origin coordinates as the rectangles. Clipping is GL state,
 * so the pending batch is flushed before it changes.
 */
void renderer_set_clip(Renderer *renderer, int x, int y, int w, int h);

/* Lifts the clipping rectangle, flushing the pending batch first. */
void renderer_clear_clip(Renderer *renderer);

/* Flushes whatever is still queued. */
void renderer_end(Renderer *renderer);

/* Releases all GL objects. Safe to call twice. */
void renderer_shutdown(Renderer *renderer);
