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
 * enum in step: the shader compares the style id against 0..5.
 *
 * RECT_STYLE_FLAT     a plain rectangle, exactly the old behaviour.
 * RECT_STYLE_BLOCK    a neon glass block: rounded corners, a glowing tube
 *                     along the edge, dark tinted glass lit from inside, a
 *                     thin inner line, and a sheen that sweeps across the
 *                     screen over time. p0 is the tube width in pixels
 *                     (0 = 15% of the shorter side). p1 is a sparkle seed: any
 *                     positive value makes the block twinkle now and then at
 *                     its own random times; 0 = no sparkles.
 * RECT_STYLE_NEON     a glowing neon tube along a rounded rectangle, or with
 *                     p0 = 0 a glowing filled rounded rectangle. The rect
 *                     includes the glow: the shape sits p1 pixels in from
 *                     every side and the glow fades out over them. p0 is the
 *                     tube width and p2 the corner radius, both in pixels.
 *                     Meant for additive blending.
 * RECT_STYLE_GLOW     a round radial falloff for particles and halos. p0 is
 *                     the hardness exponent (0 = 2.0).
 * RECT_STYLE_GHOST    one cell of a hologram silhouette: a glowing edge, a
 *                     faint fill with drifting scanlines and a flicker. The
 *                     rect is the whole grid cell. p0 is the edge width in
 *                     pixels (0 = 10% of the shorter side, at least 2), p1 is
 *                     how far the sides without a neighbor are pulled in, in
 *                     pixels, and p2 is the mask of neighboring cells that
 *                     belong to the same silhouette (RECT_GHOST_* bits).
 *                     Meant for additive blending.
 * RECT_STYLE_BEAM     a vertical beam of light, bright at the top, with
 *                     streaks falling down it. p0 and p1 are 1 when another
 *                     beam runs right next to it on the left or the right, so
 *                     only the free sides fade out. Meant for additive
 *                     blending.
 */
typedef enum RectStyle {
    RECT_STYLE_FLAT = 0,
    RECT_STYLE_BLOCK = 1,
    RECT_STYLE_NEON = 2,
    RECT_STYLE_GLOW = 3,
    RECT_STYLE_GHOST = 4,
    RECT_STYLE_BEAM = 5,
} RectStyle;

/*
 * Neighbor bits for the p2 mask of RECT_STYLE_GHOST, matched by GHOST_* in
 * quad.frag. A bit is set when that neighbor cell is part of the same
 * silhouette.
 */
enum {
    RECT_GHOST_LEFT = 1,
    RECT_GHOST_RIGHT = 2,
    RECT_GHOST_DOWN = 4,
    RECT_GHOST_UP = 8,
    RECT_GHOST_DOWN_LEFT = 16,
    RECT_GHOST_DOWN_RIGHT = 32,
    RECT_GHOST_UP_LEFT = 64,
    RECT_GHOST_UP_RIGHT = 128,
};

/* How the queued rectangles blend into the framebuffer. */
typedef enum RenderBlend {
    RENDER_BLEND_ALPHA = 0, /* src alpha, one minus src alpha: the default */
    RENDER_BLEND_ADDITIVE = 1, /* src alpha, one: glows add up */
} RenderBlend;

/*
 * Bloom: the scene is drawn into an offscreen texture, its bright parts are
 * taken down to 1/RENDERER_BLOOM_DOWNSCALE of the size and blurred
 * RENDERER_BLOOM_BLUR_PASSES times along x and then y, and the blur is added
 * back over the scene at RENDERER_BLOOM_STRENGTH. Brightness above
 * RENDERER_BLOOM_THRESHOLD (0..1, the largest color channel) blooms.
 */
#define RENDERER_BLOOM_DOWNSCALE 4
#define RENDERER_BLOOM_BLUR_PASSES 2
#define RENDERER_BLOOM_THRESHOLD 0.75f
#define RENDERER_BLOOM_STRENGTH 1.20f

/* One full-screen pass program and its uniforms; -1 for a uniform it lacks. */
typedef struct RendererPass {
    GLuint program;
    GLint resolution_uniform;
    GLint source_uniform;
    GLint source_texel_uniform; /* extract only */
    GLint threshold_uniform;    /* extract only */
    GLint direction_uniform;    /* blur only */
    GLint bloom_uniform;        /* composite only */
    GLint strength_uniform;     /* composite only */
} RendererPass;

typedef struct Renderer {
    GLuint program;
    GLuint background_program;
    GLuint vao;
    GLuint background_vao;
    GLuint quad_vbo;
    GLuint instance_vbo;
    GLint viewport_uniform;
    GLint time_uniform; /* -1 when the quad shader does not use u_time */
    GLint background_resolution_uniform;
    GLint background_time_uniform;
    int viewport_width;
    int viewport_height;
    float time_seconds; /* presentation time of the current frame */
    int capacity;
    int count;
    RenderBlend blend;
    char shader_dir[1024]; /* kept so F5 can reload from the same place; "" = built in */
    float *instances; /* capacity * RENDERER_INSTANCE_FLOATS floats */

    /*
     * Bloom state. bloom_wanted is the setting; bloom_active is whether this
     * frame is being drawn offscreen for it. The scene texture follows the
     * drawable size and the two bloom textures are the downscaled ping-pong
     * pair; all three are rebuilt when the size changes. bloom_broken is set
     * when the buffers could not be built, so the game draws without bloom
     * instead of retrying every frame.
     */
    bool bloom_wanted;
    bool bloom_active;
    bool bloom_broken;
    RendererPass bloom_extract;
    RendererPass bloom_blur;
    RendererPass bloom_composite;
    GLuint scene_fbo;
    GLuint scene_texture;
    GLuint bloom_fbo[2];
    GLuint bloom_texture[2];
    int bloom_source_width; /* drawable size the buffers were built for */
    int bloom_source_height;
} Renderer;

/*
 * Creates the shader programs (shader_dir must contain quad.vert, quad.frag,
 * background.vert and background.frag; NULL uses the copies built into the
 * executable) and the instanced quad geometry. The
 * quad program is required and a failure is fatal; the background program is
 * optional, so a broken background only logs and leaves the flat clear colour.
 * Returns false on any fatal failure.
 */
bool renderer_init(Renderer *renderer, const char *shader_dir);

/*
 * Starts a frame: sets the viewport, clears to the background color, resets the
 * blend mode to alpha and binds the quad program. All rectangles pushed
 * afterwards are in pixel coordinates with the origin in the bottom-left corner
 * of the framebuffer. time_seconds is presentation-only time: it drives the
 * background drift and the block sheen, never the game.
 *
 * With bloom on, the frame is drawn into the offscreen scene texture instead,
 * which is (re)built here whenever the drawable size changes; renderer_end puts
 * it on screen. Nothing else changes for the caller.
 */
void renderer_begin(Renderer *renderer, int fb_w, int fb_h, float time_seconds);

/*
 * Draws the full-screen animated background at the time given to
 * renderer_begin. Call it right after renderer_begin, before any rectangles.
 * Does nothing when there is no background program at all; a failed reload
 * keeps the previous one.
 */
void renderer_draw_background(Renderer *renderer);

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
 * Recompiles every shader program from shader_dir (or the built-in copies). A program that fails to
 * compile or link is logged and the old one stays in use, so a typo during
 * iteration never kills a running window. Returns whether the required quad
 * program reloaded.
 */
bool renderer_reload_shaders(Renderer *renderer);

/*
 * Flushes whatever is still queued. With bloom on it then runs the bloom passes
 * and draws the result into the default framebuffer, so the back buffer holds
 * the finished frame either way (the screenshot reads it from there).
 */
void renderer_end(Renderer *renderer);

/*
 * Turns bloom on or off from the next frame. Bloom needs the three bloom
 * shaders (bloom_extract.frag, bloom_blur.frag and bloom_composite.frag, drawn
 * with background.vert) and the offscreen buffers; when either is missing the
 * frame is drawn straight to the screen as if bloom were off.
 */
void renderer_set_bloom(Renderer *renderer, bool enabled);

/* Releases all GL objects. Safe to call twice. */
void renderer_shutdown(Renderer *renderer);
