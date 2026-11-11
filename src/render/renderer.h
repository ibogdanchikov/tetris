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

/*
 * Per-instance style, matched by value in quad.frag. Keep the numbers and the
 * enum in step: the shader compares the style id against 0..3.
 *
 * RECT_STYLE_FLAT     a plain rectangle, exactly the old behaviour.
 * RECT_STYLE_BLOCK    a polished block: vertical gradient, light top/left bevel
 *                     and a dark bottom/right one. p0 is the bevel width in
 *                     pixels (0 = 12% of the shorter side).
 * RECT_STYLE_OUTLINE  a border of p0 pixels (0 = 8% of the shorter side) and an
 *                     optional fill of p1 alpha (0.0 = hollow).
 * RECT_STYLE_GLOW     a round radial falloff for particles. p0 is the hardness
 *                     exponent (0 = 2.0).
 */
typedef enum RectStyle {
    RECT_STYLE_FLAT = 0,
    RECT_STYLE_BLOCK = 1,
    RECT_STYLE_OUTLINE = 2,
    RECT_STYLE_GLOW = 3,
} RectStyle;

/* How the queued rectangles blend into the framebuffer. */
typedef enum RenderBlend {
    RENDER_BLEND_ALPHA = 0, /* src alpha, one minus src alpha: the default */
    RENDER_BLEND_ADDITIVE = 1, /* src alpha, one: glows add up */
} RenderBlend;

typedef struct Renderer {
    GLuint program;
    GLuint background_program;
    GLuint vao;
    GLuint background_vao;
    GLuint quad_vbo;
    GLuint instance_vbo;
    GLint viewport_uniform;
    GLint background_resolution_uniform;
    GLint background_time_uniform;
    int viewport_width;
    int viewport_height;
    int capacity;
    int count;
    RenderBlend blend;
    char shader_dir[1024]; /* kept so F5 can reload from the same place */
    float *instances; /* capacity * RENDERER_INSTANCE_FLOATS floats */
} Renderer;

/*
 * Creates the shader programs (shader_dir must contain quad.vert, quad.frag,
 * background.vert and background.frag) and the instanced quad geometry. The
 * quad program is required and a failure is fatal; the background program is
 * optional, so a broken background only logs and leaves the flat clear colour.
 * Returns false on any fatal failure.
 */
bool renderer_init(Renderer *renderer, const char *shader_dir);

/*
 * Starts a frame: sets the viewport, clears to the background colour, resets the
 * blend mode to alpha and binds the quad program. All rectangles pushed
 * afterwards are in pixel coordinates with the origin in the bottom-left corner
 * of the framebuffer.
 */
void renderer_begin(Renderer *renderer, int fb_w, int fb_h);

/*
 * Draws the full-screen animated background. Call it right after renderer_begin,
 * before any rectangles; time_seconds drives the drift. Does nothing when there
 * is no background program at all; a failed reload keeps the previous one.
 */
void renderer_draw_background(Renderer *renderer, float time_seconds);

/* Queues one flat axis-aligned rectangle. Flushes early if the batch is full. */
void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color);

/*
 * Queues one rectangle with a style. p0..p2 are the style parameters described
 * on RectStyle; RECT_STYLE_FLAT ignores them.
 */
void renderer_push_styled(Renderer *renderer, float x, float y, float w, float h, Color color,
                          RectStyle style, float p0, float p1, float p2);

/*
 * Restricts everything drawn from now on to the given pixel rectangle, in the
 * same bottom-left-origin coordinates as the rectangles. Clipping is GL state,
 * so the pending batch is flushed before it changes.
 */
void renderer_set_clip(Renderer *renderer, int x, int y, int w, int h);

/* Lifts the clipping rectangle, flushing the pending batch first. */
void renderer_clear_clip(Renderer *renderer);

/*
 * Switches the blend mode, flushing the pending batch first because blending is
 * GL state. renderer_begin resets it to RENDER_BLEND_ALPHA.
 */
void renderer_set_blend(Renderer *renderer, RenderBlend blend);

/*
 * Recompiles every shader program from shader_dir. A program that fails to
 * compile or link is logged and the old one stays in use, so a typo during
 * iteration never kills a running window. Returns whether the required quad
 * program reloaded.
 */
bool renderer_reload_shaders(Renderer *renderer);

/* Flushes whatever is still queued. */
void renderer_end(Renderer *renderer);

/* Releases all GL objects. Safe to call twice. */
void renderer_shutdown(Renderer *renderer);
