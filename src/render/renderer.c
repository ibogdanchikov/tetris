#include "render/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/builtin_shaders.h"
#include "render/shader.h"
#include "render/stroke_font.h"

/* Per-instance payload: rect (x, y, w, h), colour (r, g, b, a), style params. */
#define RENDERER_INSTANCE_FLOATS 12

/* Shader paths are the stored directory plus a file name; leave room for both. */
#define RENDERER_SHADER_PATH_MAX 1280

#define RENDERER_BACKGROUND_R 0.04f
#define RENDERER_BACKGROUND_G 0.04f
#define RENDERER_BACKGROUND_B 0.06f
#define RENDERER_BACKGROUND_A 1.0f

static void renderer_flush(Renderer *renderer)
{
    if (renderer->count <= 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, renderer->instance_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)((size_t)renderer->count * RENDERER_INSTANCE_FLOATS * sizeof(float)),
                    renderer->instances);

    glBindVertexArray(renderer->vao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, renderer->count);

    renderer->count = 0;
}

static void renderer_apply_blend(Renderer *renderer)
{
    switch (renderer->blend) {
    case RENDER_BLEND_ADDITIVE:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case RENDER_BLEND_ALPHA:
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

/*
 * Compiles and links one program from the shader directory, or from the copies
 * built into the executable when the renderer has no shader directory.
 * Returns 0 on failure.
 */
static GLuint renderer_build_program(const Renderer *renderer, const char *vert_name,
                                     const char *frag_name)
{
    char vert_path[RENDERER_SHADER_PATH_MAX];
    char frag_path[RENDERER_SHADER_PATH_MAX];

    if (renderer->shader_dir[0] == '\0') {
        const char *vert_source = builtin_shader_source(vert_name);
        const char *frag_source = builtin_shader_source(frag_name);
        if (vert_source == NULL || frag_source == NULL) {
            fprintf(stderr, "renderer: %s or %s is not built in\n", vert_name, frag_name);
            return 0;
        }
        snprintf(vert_path, sizeof vert_path, "<built-in>/%s", vert_name);
        snprintf(frag_path, sizeof frag_path, "<built-in>/%s", frag_name);
        return shader_build_program(vert_source, frag_source, vert_path, frag_path);
    }

    snprintf(vert_path, sizeof vert_path, "%s/%s", renderer->shader_dir, vert_name);
    snprintf(frag_path, sizeof frag_path, "%s/%s", renderer->shader_dir, frag_name);
    return shader_load_program(vert_path, frag_path);
}

/*
 * Loads quad.vert / quad.frag into renderer->program. On any failure the old
 * program stays bound, which is what makes F5 safe to hammer over a typo.
 */
static bool renderer_load_quad_program(Renderer *renderer)
{
    const GLuint program = renderer_build_program(renderer, "quad.vert", "quad.frag");
    if (program == 0) {
        return false;
    }

    const GLint viewport_uniform = glGetUniformLocation(program, "u_viewport_size");
    if (viewport_uniform < 0) {
        fprintf(stderr, "renderer: shader is missing uniform u_viewport_size\n");
        glDeleteProgram(program);
        return false;
    }

    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
    }
    renderer->program = program;
    renderer->viewport_uniform = viewport_uniform;
    /* Optional: a shader without u_time gets -1, and setting -1 is a no-op. */
    renderer->time_uniform = glGetUniformLocation(program, "u_time");
    /* So are the font uniforms; without them text uses the pixel font. */
    renderer->font_uniform = glGetUniformLocation(program, "u_font");
    renderer->font_grid_uniform = glGetUniformLocation(program, "u_font_grid");
    return true;
}

/*
 * Loads background.vert / background.frag. Unlike the quad this is optional: a
 * failure keeps the previous program (or none at all) and the flat clear colour
 * is the fallback.
 */
static void renderer_load_background_program(Renderer *renderer)
{
    const GLuint program = renderer_build_program(renderer, "background.vert", "background.frag");
    if (program == 0) {
        fprintf(stderr, "renderer: background shaders failed to load; %s\n",
                renderer->background_program != 0 ? "keeping the previous program"
                                                  : "using the flat clear colour");
        return;
    }

    const GLint resolution_uniform = glGetUniformLocation(program, "u_resolution");
    const GLint time_uniform = glGetUniformLocation(program, "u_time");
    if (resolution_uniform < 0 || time_uniform < 0) {
        fprintf(stderr, "renderer: background shader is missing u_resolution or u_time; %s\n",
                renderer->background_program != 0 ? "keeping the previous program"
                                                  : "using the flat clear colour");
        glDeleteProgram(program);
        return;
    }

    if (renderer->background_program != 0) {
        glDeleteProgram(renderer->background_program);
    }
    renderer->background_program = program;
    renderer->background_resolution_uniform = resolution_uniform;
    renderer->background_time_uniform = time_uniform;
}

/*
 * Loads one full-screen pass: background.vert (the full-screen triangle) with a
 * bloom fragment shader. source_name is the sampler the pass reads its main
 * input from. A failure keeps the previous program, like the background.
 */
static void renderer_load_pass(Renderer *renderer, const char *frag_name,
                               const char *source_name, RendererPass *pass)
{
    const GLuint program = renderer_build_program(renderer, "background.vert", frag_name);
    if (program == 0) {
        fprintf(stderr, "renderer: %s failed to load; %s\n", frag_name,
                pass->program != 0 ? "keeping the previous program" : "no bloom");
        return;
    }

    RendererPass loaded;
    loaded.program = program;
    loaded.resolution_uniform = glGetUniformLocation(program, "u_resolution");
    loaded.source_uniform = glGetUniformLocation(program, source_name);
    loaded.source_texel_uniform = glGetUniformLocation(program, "u_source_texel");
    loaded.threshold_uniform = glGetUniformLocation(program, "u_threshold");
    loaded.direction_uniform = glGetUniformLocation(program, "u_direction");
    loaded.bloom_uniform = glGetUniformLocation(program, "u_bloom");
    loaded.strength_uniform = glGetUniformLocation(program, "u_strength");
    if (loaded.resolution_uniform < 0 || loaded.source_uniform < 0) {
        fprintf(stderr, "renderer: %s is missing u_resolution or %s; %s\n", frag_name,
                source_name, pass->program != 0 ? "keeping the previous program" : "no bloom");
        glDeleteProgram(program);
        return;
    }

    if (pass->program != 0) {
        glDeleteProgram(pass->program);
    }
    *pass = loaded;
}

static void renderer_load_bloom_programs(Renderer *renderer)
{
    renderer_load_pass(renderer, "bloom_extract.frag", "u_source", &renderer->bloom_extract);
    renderer_load_pass(renderer, "bloom_blur.frag", "u_source", &renderer->bloom_blur);
    renderer_load_pass(renderer, "bloom_composite.frag", "u_scene", &renderer->bloom_composite);
}

static bool renderer_bloom_programs_ready(const Renderer *renderer)
{
    return renderer->bloom_extract.program != 0 && renderer->bloom_blur.program != 0 &&
           renderer->bloom_composite.program != 0;
}

/* One side of the bloom textures: the scene side downscaled, at least 1. */
static int renderer_bloom_size(int scene_size)
{
    const int size = scene_size / RENDERER_BLOOM_DOWNSCALE;
    return size > 0 ? size : 1;
}

static void renderer_delete_bloom_buffers(Renderer *renderer)
{
    if (renderer->scene_fbo != 0) {
        glDeleteFramebuffers(1, &renderer->scene_fbo);
    }
    if (renderer->scene_texture != 0) {
        glDeleteTextures(1, &renderer->scene_texture);
    }
    for (int i = 0; i < 2; ++i) {
        if (renderer->bloom_fbo[i] != 0) {
            glDeleteFramebuffers(1, &renderer->bloom_fbo[i]);
        }
        if (renderer->bloom_texture[i] != 0) {
            glDeleteTextures(1, &renderer->bloom_texture[i]);
        }
        renderer->bloom_fbo[i] = 0;
        renderer->bloom_texture[i] = 0;
    }
    renderer->scene_fbo = 0;
    renderer->scene_texture = 0;
    renderer->bloom_source_width = 0;
    renderer->bloom_source_height = 0;
}

/* One render target: a linearly filtered texture and the framebuffer that draws
   into it. Returns whether the framebuffer is complete. */
static bool renderer_make_target(int width, int height, GLint internal_format, GLenum type,
                                 GLuint *out_texture, GLuint *out_fbo)
{
    glGenTextures(1, out_texture);
    glBindTexture(GL_TEXTURE_2D, *out_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, GL_RGBA, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *out_texture, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/*
 * Builds the scene texture at the drawable size and the two bloom textures at
 * a quarter of it. The bloom textures are half floats, so the faint ends of
 * the blur do not band.
 */
static bool renderer_build_bloom_buffers(Renderer *renderer, int width, int height)
{
    renderer_delete_bloom_buffers(renderer);

    const int bloom_width = renderer_bloom_size(width);
    const int bloom_height = renderer_bloom_size(height);

    bool complete = renderer_make_target(width, height, GL_RGBA8, GL_UNSIGNED_BYTE,
                                         &renderer->scene_texture, &renderer->scene_fbo);
    for (int i = 0; i < 2; ++i) {
        complete = renderer_make_target(bloom_width, bloom_height, GL_RGBA16F, GL_FLOAT,
                                        &renderer->bloom_texture[i], &renderer->bloom_fbo[i]) &&
                   complete;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!complete) {
        fprintf(stderr, "renderer: bloom framebuffers are incomplete at %dx%d; no bloom\n",
                width, height);
        renderer_delete_bloom_buffers(renderer);
        return false;
    }
    renderer->bloom_source_width = width;
    renderer->bloom_source_height = height;
    return true;
}

/*
 * The bloom passes, from the scene texture to the default framebuffer: the
 * bright parts at a quarter of the size, a blur along x and y (twice), then the
 * scene with the blur added. Blending and the clip are off for all of them.
 */
static void renderer_run_bloom(Renderer *renderer)
{
    const int width = renderer->bloom_source_width;
    const int height = renderer->bloom_source_height;
    const int bloom_width = renderer_bloom_size(width);
    const int bloom_height = renderer_bloom_size(height);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(renderer->background_vao);
    glActiveTexture(GL_TEXTURE0);

    const RendererPass *extract = &renderer->bloom_extract;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->bloom_fbo[0]);
    glViewport(0, 0, bloom_width, bloom_height);
    glUseProgram(extract->program);
    glUniform2f(extract->resolution_uniform, (float)bloom_width, (float)bloom_height);
    glUniform1i(extract->source_uniform, 0);
    glUniform2f(extract->source_texel_uniform, 1.0f / (float)width, 1.0f / (float)height);
    glUniform1f(extract->threshold_uniform, RENDERER_BLOOM_THRESHOLD);
    glBindTexture(GL_TEXTURE_2D, renderer->scene_texture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    const RendererPass *blur = &renderer->bloom_blur;
    glUseProgram(blur->program);
    glUniform2f(blur->resolution_uniform, (float)bloom_width, (float)bloom_height);
    glUniform1i(blur->source_uniform, 0);
    for (int pass = 0; pass < RENDERER_BLOOM_BLUR_PASSES; ++pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->bloom_fbo[1]);
        glBindTexture(GL_TEXTURE_2D, renderer->bloom_texture[0]);
        glUniform2f(blur->direction_uniform, 1.0f / (float)bloom_width, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, renderer->bloom_fbo[0]);
        glBindTexture(GL_TEXTURE_2D, renderer->bloom_texture[1]);
        glUniform2f(blur->direction_uniform, 0.0f, 1.0f / (float)bloom_height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    const RendererPass *composite = &renderer->bloom_composite;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(composite->program);
    glUniform2f(composite->resolution_uniform, (float)width, (float)height);
    glUniform1i(composite->source_uniform, 0);
    glUniform1i(composite->bloom_uniform, 1);
    glUniform1f(composite->strength_uniform, RENDERER_BLOOM_STRENGTH);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer->bloom_texture[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->scene_texture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
}

/*
 * Builds the stroke font's distance atlas on the CPU and uploads it as a
 * one-channel texture with mipmaps, so small text stays smooth. A failure only
 * logs: text falls back to the pixel font.
 */
static void renderer_build_font(Renderer *renderer)
{
    int width = 0;
    int height = 0;
    stroke_font_atlas_size(&width, &height);
    unsigned char *pixels = malloc((size_t)width * (size_t)height);
    if (pixels == NULL || !stroke_font_build_atlas(pixels, width, height)) {
        fprintf(stderr, "renderer: could not build the font atlas; using the pixel font\n");
        free(pixels);
        return;
    }

    glActiveTexture(GL_TEXTURE0 + RENDERER_FONT_UNIT);
    glGenTextures(1, &renderer->font_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->font_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    free(pixels);

    int cell_width = 0;
    int cell_height = 0;
    stroke_font_cell_size(&cell_width, &cell_height);
    renderer->font_columns = width / cell_width;
    renderer->font_rows = height / cell_height;
}

bool renderer_has_font(const Renderer *renderer)
{
    return renderer->font_texture != 0 && renderer->font_uniform >= 0 &&
           renderer->font_grid_uniform >= 0;
}

void renderer_set_bloom(Renderer *renderer, bool enabled)
{
    renderer->bloom_wanted = enabled;
    if (!enabled) {
        /* Free the buffers; turning bloom back on builds them again. */
        renderer_delete_bloom_buffers(renderer);
    }
}

bool renderer_init(Renderer *renderer, const char *shader_dir)
{
    memset(renderer, 0, sizeof *renderer);
    renderer->capacity = RENDERER_MAX_RECTS;
    renderer->blend = RENDER_BLEND_ALPHA;

    snprintf(renderer->shader_dir, sizeof renderer->shader_dir, "%s",
             shader_dir != NULL ? shader_dir : "");

    renderer->instances = calloc((size_t)renderer->capacity * RENDERER_INSTANCE_FLOATS, sizeof(float));
    if (renderer->instances == NULL) {
        fprintf(stderr, "renderer: out of memory for %d rects\n", renderer->capacity);
        return false;
    }

    if (!renderer_load_quad_program(renderer)) {
        renderer_shutdown(renderer);
        return false;
    }

    /* The background is best effort: the flat clear is a perfectly good fallback. */
    renderer_load_background_program(renderer);

    /* So is bloom: without its shaders the frame goes straight to the screen. */
    renderer_load_bloom_programs(renderer);

    /* And the font atlas: without it text uses the pixel font. */
    renderer_build_font(renderer);

    /* Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1). */
    static const float quad_vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glGenVertexArrays(1, &renderer->vao);
    glGenBuffers(1, &renderer->quad_vbo);
    glGenBuffers(1, &renderer->instance_vbo);
    /* The background triangle writes its corners from gl_VertexID, so its VAO
       only has to exist; it never needs a buffer. */
    glGenVertexArrays(1, &renderer->background_vao);
    if (renderer->vao == 0 || renderer->background_vao == 0 || renderer->quad_vbo == 0 ||
        renderer->instance_vbo == 0) {
        fprintf(stderr, "renderer: failed to create GL buffers\n");
        renderer_shutdown(renderer);
        return false;
    }

    glBindVertexArray(renderer->vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof quad_vertices, quad_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(float), (const void *)0);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)renderer->capacity * RENDERER_INSTANCE_FLOATS * sizeof(float)),
                 NULL, GL_DYNAMIC_DRAW);

    const GLsizei instance_stride = RENDERER_INSTANCE_FLOATS * (GLsizei)sizeof(float);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, instance_stride, (const void *)0);
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, instance_stride,
                          (const void *)(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, instance_stride,
                          (const void *)(8 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void renderer_begin(Renderer *renderer, int fb_w, int fb_h, float time_seconds)
{
    renderer->viewport_width = fb_w;
    renderer->viewport_height = fb_h;
    renderer->time_seconds = time_seconds;

    /* With bloom the frame goes into the scene texture, which follows the
       drawable size; renderer_end puts it on screen. */
    renderer->bloom_active = false;
    if (renderer->bloom_wanted && !renderer->bloom_broken && fb_w > 0 && fb_h > 0 &&
        renderer_bloom_programs_ready(renderer)) {
        if ((fb_w != renderer->bloom_source_width || fb_h != renderer->bloom_source_height) &&
            !renderer_build_bloom_buffers(renderer, fb_w, fb_h)) {
            renderer->bloom_broken = true;
        }
        renderer->bloom_active = !renderer->bloom_broken;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->bloom_active ? renderer->scene_fbo : 0);

    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    renderer->blend = RENDER_BLEND_ALPHA;
    renderer_apply_blend(renderer);
    glClearColor(RENDERER_BACKGROUND_R, RENDERER_BACKGROUND_G, RENDERER_BACKGROUND_B,
                 RENDERER_BACKGROUND_A);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer->count = 0;

    glUseProgram(renderer->program);
    glUniform2f(renderer->viewport_uniform, (float)fb_w, (float)fb_h);
    glUniform1f(renderer->time_uniform, time_seconds);

    /* The atlas is bound every frame, so nothing else can leave the unit
       pointing at another texture. */
    if (renderer->font_texture != 0) {
        glActiveTexture(GL_TEXTURE0 + RENDERER_FONT_UNIT);
        glBindTexture(GL_TEXTURE_2D, renderer->font_texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(renderer->font_uniform, RENDERER_FONT_UNIT);
        glUniform2f(renderer->font_grid_uniform, (float)renderer->font_columns,
                    (float)renderer->font_rows);
    }
}

void renderer_draw_background(Renderer *renderer)
{
    if (renderer->background_program == 0) {
        return;
    }

    /* Nothing is queued yet by contract, but a flush keeps the order honest. */
    renderer_flush(renderer);

    glUseProgram(renderer->background_program);
    glUniform2f(renderer->background_resolution_uniform, (float)renderer->viewport_width,
                (float)renderer->viewport_height);
    glUniform1f(renderer->background_time_uniform, renderer->time_seconds);

    glBindVertexArray(renderer->background_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(renderer->program);
}

void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color)
{
    renderer_push_styled(renderer, x, y, w, h, color, RECT_STYLE_FLAT, 0.0f, 0.0f, 0.0f);
}

void renderer_push_styled(Renderer *renderer, float x, float y, float w, float h, Color color,
                          RectStyle style, float p0, float p1, float p2)
{
    if (renderer->count >= renderer->capacity) {
        renderer_flush(renderer);
    }

    float *instance = renderer->instances +
                      (size_t)renderer->count * RENDERER_INSTANCE_FLOATS;
    instance[0] = x;
    instance[1] = y;
    instance[2] = w;
    instance[3] = h;
    instance[4] = color.r;
    instance[5] = color.g;
    instance[6] = color.b;
    instance[7] = color.a;
    instance[8] = (float)style;
    instance[9] = p0;
    instance[10] = p1;
    instance[11] = p2;
    ++renderer->count;
}

void renderer_set_clip(Renderer *renderer, int x, int y, int w, int h)
{
    renderer_flush(renderer);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w > 0 ? w : 0, h > 0 ? h : 0);
}

void renderer_clear_clip(Renderer *renderer)
{
    renderer_flush(renderer);
    glDisable(GL_SCISSOR_TEST);
}

void renderer_set_blend(Renderer *renderer, RenderBlend blend)
{
    if (renderer->blend == blend) {
        return;
    }
    renderer_flush(renderer);
    renderer->blend = blend;
    renderer_apply_blend(renderer);
}

bool renderer_reload_shaders(Renderer *renderer)
{
    const bool quad_ok = renderer_load_quad_program(renderer);
    if (!quad_ok) {
        fprintf(stderr, "renderer: quad shaders failed to reload, keeping the old program\n");
    }

    renderer_load_background_program(renderer);
    renderer_load_bloom_programs(renderer);

    if (renderer->program != 0) {
        glUseProgram(renderer->program);
        glUniform2f(renderer->viewport_uniform, (float)renderer->viewport_width,
                    (float)renderer->viewport_height);
        glUniform1f(renderer->time_uniform, renderer->time_seconds);
    }
    return quad_ok;
}

void renderer_end(Renderer *renderer)
{
    renderer_flush(renderer);
    if (renderer->bloom_active) {
        renderer_run_bloom(renderer);
    }
    glUseProgram(0);
}

void renderer_shutdown(Renderer *renderer)
{
    renderer_delete_bloom_buffers(renderer);
    if (renderer->font_texture != 0) {
        glDeleteTextures(1, &renderer->font_texture);
        renderer->font_texture = 0;
    }
    RendererPass *passes[] = { &renderer->bloom_extract, &renderer->bloom_blur,
                               &renderer->bloom_composite };
    for (size_t i = 0; i < sizeof passes / sizeof passes[0]; ++i) {
        if (passes[i]->program != 0) {
            glDeleteProgram(passes[i]->program);
            passes[i]->program = 0;
        }
    }
    if (renderer->instance_vbo != 0) {
        glDeleteBuffers(1, &renderer->instance_vbo);
        renderer->instance_vbo = 0;
    }
    if (renderer->quad_vbo != 0) {
        glDeleteBuffers(1, &renderer->quad_vbo);
        renderer->quad_vbo = 0;
    }
    if (renderer->vao != 0) {
        glDeleteVertexArrays(1, &renderer->vao);
        renderer->vao = 0;
    }
    if (renderer->background_vao != 0) {
        glDeleteVertexArrays(1, &renderer->background_vao);
        renderer->background_vao = 0;
    }
    if (renderer->background_program != 0) {
        glDeleteProgram(renderer->background_program);
        renderer->background_program = 0;
    }
    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
        renderer->program = 0;
    }
    free(renderer->instances);
    renderer->instances = NULL;
    renderer->count = 0;
    renderer->capacity = 0;
}
